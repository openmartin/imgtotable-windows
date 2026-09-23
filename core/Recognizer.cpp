#include "Recognizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <string_view>

#ifdef IMGTOTABLE_HAS_ONNX
#include <onnxruntime_cxx_api.h>
#endif

namespace {
#ifdef IMGTOTABLE_HAS_ONNX
struct Box { int x, y, w, h; };
#endif

bool valid(const RecognitionImage& image) {
    return image.width > 0 && image.height > 0 &&
        static_cast<std::uint64_t>(image.width) * image.height * 3 == image.rgb.size();
}

#ifdef IMGTOTABLE_HAS_ONNX
std::vector<std::string> dictionary(const std::filesystem::path& file) {
    std::ifstream stream(file);
    if (!stream) throw std::runtime_error("Cannot open character dictionary: " + file.string());
    std::vector<std::string> chars;
    std::string line;
    bool inDict = false;
    while (std::getline(stream, line)) {
        if (!inDict) { inDict = line == "  character_dict:"; continue; }
        if (line.rfind("  - ", 0) != 0) break;
        std::string value = line.substr(4);
        if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
            value = value.substr(1, value.size() - 2);
            std::string::size_type pos = 0;
            while ((pos = value.find("''", pos)) != std::string::npos) { value.replace(pos, 2, "'"); ++pos; }
        } else if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        chars.push_back(std::move(value));
    }
    return chars;
}

RecognitionImage crop(const RecognitionImage& image, Box b) {
    b.x = std::clamp(b.x, 0, image.width);
    b.y = std::clamp(b.y, 0, image.height);
    b.w = std::clamp(b.w, 0, image.width - b.x);
    b.h = std::clamp(b.h, 0, image.height - b.y);
    RecognitionImage result{b.w, b.h, std::vector<std::uint8_t>(static_cast<size_t>(b.w) * b.h * 3)};
    for (int y = 0; y < b.h; ++y) {
        std::copy_n(image.rgb.data() + (static_cast<size_t>(y + b.y) * image.width + b.x) * 3,
                    static_cast<size_t>(b.w) * 3, result.rgb.data() + static_cast<size_t>(y) * b.w * 3);
    }
    return result;
}

// Bilinear resize to normalized BGR CHW. Padding remains zero after normalization.
std::vector<float> tensor(const RecognitionImage& image, int outW, int outH, int activeW, int activeH,
                          bool imagenet) {
    std::vector<float> out(static_cast<size_t>(3) * outW * outH);
    const std::array<float, 3> mean{0.485F, 0.456F, 0.406F};
    const std::array<float, 3> stddev{0.229F, 0.224F, 0.225F};
    for (int y = 0; y < activeH; ++y) {
        const float fy = (y + .5F) * image.height / activeH - .5F;
        int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, image.height - 1);
        int y1 = std::min(y0 + 1, image.height - 1);
        float dy = std::clamp(fy - std::floor(fy), 0.F, 1.F);
        for (int x = 0; x < activeW; ++x) {
            const float fx = (x + .5F) * image.width / activeW - .5F;
            int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, image.width - 1);
            int x1 = std::min(x0 + 1, image.width - 1);
            float dx = std::clamp(fx - std::floor(fx), 0.F, 1.F);
            for (int c = 0; c < 3; ++c) {
                const int rgbChannel = 2 - c;
                auto at = [&](int ix, int iy) { return image.rgb[(static_cast<size_t>(iy) * image.width + ix) * 3 + rgbChannel]; };
                float pixel = (1 - dy) * ((1 - dx) * at(x0, y0) + dx * at(x1, y0)) +
                              dy * ((1 - dx) * at(x0, y1) + dx * at(x1, y1));
                float value = pixel / 255.F;
                out[(static_cast<size_t>(c) * outH + y) * outW + x] =
                    imagenet ? (value - mean[c]) / stddev[c] : (value - .5F) / .5F;
            }
        }
    }
    return out;
}

std::vector<Box> boxesFromMap(const float* map, int mapW, int mapH, int imgW, int imgH) {
    std::vector<std::uint8_t> seen(static_cast<size_t>(mapW) * mapH);
    std::vector<Box> boxes;
    const int minArea = std::max(6, mapW * mapH / 200000);
    for (int y = 0; y < mapH; ++y) for (int x = 0; x < mapW; ++x) {
        const int start = y * mapW + x;
        if (seen[start] || map[start] < .2F) continue;
        std::queue<int> q; q.push(start); seen[start] = 1;
        int left=x, right=x, top=y, bottom=y, count=0;
        float sum=0;
        while (!q.empty()) {
            int id=q.front(); q.pop(); int px=id%mapW, py=id/mapW;
            ++count; sum += map[id];
            left=std::min(left,px); right=std::max(right,px);
            top=std::min(top,py); bottom=std::max(bottom,py);
            for (auto [nx,ny] : {std::pair{px-1,py}, {px+1,py}, {px,py-1}, {px,py+1}}) {
                if (nx<0 || nx>=mapW || ny<0 || ny>=mapH) continue;
                int next=ny*mapW+nx;
                if (!seen[next] && map[next]>=.2F) { seen[next]=1; q.push(next); }
            }
        }
        if (count < minArea || sum/count < .45F) continue;
        float sx=static_cast<float>(imgW)/mapW, sy=static_cast<float>(imgH)/mapH;
        int padX=std::max(2, static_cast<int>((right-left+1)*sx*.15F));
        int padY=std::max(2, static_cast<int>((bottom-top+1)*sy*.20F));
        int bx=std::max(0,static_cast<int>(left*sx)-padX), by=std::max(0,static_cast<int>(top*sy)-padY);
        int ex=std::min(imgW,static_cast<int>((right+1)*sx)+padX), ey=std::min(imgH,static_cast<int>((bottom+1)*sy)+padY);
        boxes.push_back({bx,by,ex-bx,ey-by});
    }
    std::stable_sort(boxes.begin(), boxes.end(), [](const Box& a,const Box& b) {
        int line=std::max(5,std::min(a.h,b.h)/2);
        return std::abs(a.y-b.y)>line ? a.y<b.y : a.x<b.x;
    });
    return boxes;
}
#endif

std::string csvEscape(std::string_view value) {
    if (value.find_first_of(",\"\r\n") == std::string_view::npos) return std::string(value);
    std::string out="\"";
    for (char c:value) { if(c=='\"') out+='\"'; out+=c; }
    out+='\"'; return out;
}
}

struct Recognizer::Impl {
#ifdef IMGTOTABLE_HAS_ONNX
    Ort::ThreadingOptions threads;
    Ort::Env env{threads, ORT_LOGGING_LEVEL_WARNING, "ImgToTable"};
    Ort::Session det{nullptr}, rec{nullptr}, table{nullptr};
#endif
    std::vector<std::string> recChars, tableChars;
};

Recognizer::Recognizer(std::filesystem::path directory) : directory_(std::move(directory)) {}
Recognizer::~Recognizer() = default;
const std::string& Recognizer::lastError() const noexcept { return error_; }
bool Recognizer::isLoaded() const noexcept { return impl_ != nullptr; }

bool Recognizer::load() {
    impl_.reset(); error_.clear();
#ifdef IMGTOTABLE_HAS_ONNX
    try {
        auto state=std::make_unique<Impl>();
        state->recChars=dictionary(directory_/"PP-OCRv6_small_rec"/"inference.yml");
        state->tableChars=dictionary(directory_/"SLANet_plus"/"inference.yml");
        // TableLabelDecode's merge_no_span_structure replaces <td> with this token.
        auto td=std::find(state->tableChars.begin(),state->tableChars.end(),"<td>");
        if(td!=state->tableChars.end()) state->tableChars.erase(td);
        state->tableChars.push_back("<td></td>");
        if (state->recChars.size()+2 != 18710 || state->tableChars.size()+2 != 50)
            throw std::runtime_error("Model dictionary size does not match output classes.");
        Ort::SessionOptions options;
        options.DisablePerSessionThreads();
        options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        state->det=Ort::Session(state->env,(directory_/"PP-OCRv6_small_det"/"inference.onnx").c_str(),options);
        state->rec=Ort::Session(state->env,(directory_/"PP-OCRv6_small_rec"/"inference.onnx").c_str(),options);
        state->table=Ort::Session(state->env,(directory_/"SLANet_plus"/"inference.onnx").c_str(),options);
        for (auto* session:{&state->det,&state->rec,&state->table}) {
            if(session->GetInputCount()!=1) throw std::runtime_error("Model must have one image input.");
            auto inputInfo=session->GetInputTypeInfo(0);
            if(inputInfo.GetONNXType()!=ONNX_TYPE_TENSOR) throw std::runtime_error("Model input is not a tensor.");
            auto inputType=inputInfo.GetTensorTypeAndShapeInfo();
            auto shape=inputType.GetShape();
            if(inputType.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || shape.size()!=4 || shape[1]!=3)
                throw std::runtime_error("Model input must be float32 NCHW with three channels.");
            for(size_t i=0;i<session->GetOutputCount();++i) {
                auto outputInfo=session->GetOutputTypeInfo(i);
                if(outputInfo.GetONNXType()!=ONNX_TYPE_TENSOR ||
                   outputInfo.GetTensorTypeAndShapeInfo().GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
                    throw std::runtime_error("Model outputs must be float32 tensors.");
            }
        }
        if(state->det.GetOutputCount()!=1 || state->rec.GetOutputCount()!=1 || state->table.GetOutputCount()!=2)
            throw std::runtime_error("Unexpected model output count.");
        impl_=std::move(state); return true;
    } catch(const std::exception& e) { error_=e.what(); }
#else
    error_="ONNX Runtime is not configured. Set ONNXRUNTIME_ROOT and rebuild.";
#endif
    return false;
}

#ifdef IMGTOTABLE_HAS_ONNX
namespace {
std::vector<Ort::Value> run(Ort::Session& session, std::vector<float>& values, int h, int w) {
    const std::array<std::int64_t,4> shape{1,3,h,w};
    auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
    auto input=Ort::Value::CreateTensor<float>(memory,values.data(),values.size(),shape.data(),shape.size());
    Ort::AllocatorWithDefaultOptions allocator;
    auto inputName=session.GetInputNameAllocated(0,allocator);
    std::vector<Ort::AllocatedStringPtr> names;
    std::vector<const char*> outputNames;
    for(size_t i=0;i<session.GetOutputCount();++i) { names.push_back(session.GetOutputNameAllocated(i,allocator)); outputNames.push_back(names.back().get()); }
    const char* inputNames[]={inputName.get()};
    return session.Run(Ort::RunOptions{nullptr},inputNames,&input,1,outputNames.data(),outputNames.size());
}
}
#endif

bool Recognizer::recognizeText(const RecognitionImage& image, std::vector<TextRegion>& result) {
    result.clear(); error_.clear();
    if (!impl_) { error_="Models are not loaded."; return false; }
    if (!valid(image)) { error_="Invalid RGB image."; return false; }
#ifdef IMGTOTABLE_HAS_ONNX
    try {
        // DB detection uses dimensions divisible by 32, preserving aspect ratio.
        float scale=std::min(1.F,960.F/std::max(image.width,image.height));
        int w=std::max(32,static_cast<int>(std::round(image.width*scale/32))*32);
        int h=std::max(32,static_cast<int>(std::round(image.height*scale/32))*32);
        auto input=tensor(image,w,h,w,h,true);
        auto detected=run(impl_->det,input,h,w);
        auto shape=detected[0].GetTensorTypeAndShapeInfo().GetShape();
        if(shape.size()!=4 || shape[0]!=1 || shape[1]!=1 || shape[2]<=0 || shape[3]<=0)
            throw std::runtime_error("Unexpected detection output shape.");
        auto regions=boxesFromMap(detected[0].GetTensorData<float>(),static_cast<int>(shape[3]),static_cast<int>(shape[2]),image.width,image.height);
        for (const auto& box:regions) {
            if(box.w<2 || box.h<2) continue;
            auto part=crop(image,box);
            // The recognizer accepts dynamic widths. Keep long text at its
            // natural aspect ratio instead of squeezing every line into 320px.
            int scaledW=std::clamp(static_cast<int>(std::ceil(48.F*part.width/part.height)),1,3200);
            int recW=std::max(320,scaledW);
            auto recInput=tensor(part,recW,48,scaledW,48,false);
            auto recognized=run(impl_->rec,recInput,48,recW);
            auto recShape=recognized[0].GetTensorTypeAndShapeInfo().GetShape();
            if(recShape.size()!=3 || recShape[0]!=1 || recShape[2]!=18710) throw std::runtime_error("Unexpected recognition output shape.");
            const float* scores=recognized[0].GetTensorData<float>();
            std::string text;
            float confidence=0; int accepted=0, prev=-1;
            for(int t=0;t<recShape[1];++t) {
                const float* row=scores+static_cast<size_t>(t)*18710;
                int id=static_cast<int>(std::max_element(row,row+18710)-row);
                if(id>0 && id!=prev) {
                    if(id<=static_cast<int>(impl_->recChars.size())) text+=impl_->recChars[id-1];
                    else if(id==static_cast<int>(impl_->recChars.size())+1) text+=' ';
                    confidence+=row[id]; ++accepted;
                }
                prev=id;
            }
            if(!text.empty()) result.push_back({box.x,box.y,box.w,box.h,std::move(text),accepted?confidence/accepted:0});
        }
        return true;
    } catch(const std::exception& e) { result.clear(); error_=e.what(); }
#endif
    return false;
}

bool Recognizer::recognizeTable(const RecognitionImage& image, TableResult& result) {
    result={}; error_.clear();
    if (!impl_) { error_="Models are not loaded."; return false; }
    if (!valid(image)) { error_="Invalid RGB image."; return false; }
#ifdef IMGTOTABLE_HAS_ONNX
    try {
        float scale=488.F/std::max(image.width,image.height);
        int activeW=std::clamp(static_cast<int>(std::round(image.width*scale)),1,488);
        int activeH=std::clamp(static_cast<int>(std::round(image.height*scale)),1,488);
        auto input=tensor(image,488,488,activeW,activeH,true);
        auto outputs=run(impl_->table,input,488,488);
        auto boxShape=outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        auto tokenShape=outputs[1].GetTensorTypeAndShapeInfo().GetShape();
        if(boxShape.size()!=3 || tokenShape.size()!=3 || boxShape[0]!=1 || tokenShape[0]!=1 ||
           boxShape[1]!=tokenShape[1] || boxShape[2]!=8 || tokenShape[2]!=50)
            throw std::runtime_error("Unexpected table output shape.");
        const float* boxes=outputs[0].GetTensorData<float>();
        const float* scores=outputs[1].GetTensorData<float>();
        std::vector<TextRegion> words;
        if(!recognizeText(image,words)) throw std::runtime_error("Table OCR failed: "+error_);
        int row=-1, col=0;
        for(int t=0;t<tokenShape[1];++t) {
            const float* token=scores+static_cast<size_t>(t)*50;
            int id=static_cast<int>(std::max_element(token,token+50)-token);
            if(id==49) break; // eos
            if(id==0) continue; // sos
            const std::string& symbol=impl_->tableChars[id-1];
            if(symbol=="<tr>") { ++row; col=0; result.rows=std::max(result.rows,row+1); continue; }
            if(symbol=="</tr>") continue;
            if(symbol!="<td>" && symbol!="<td" && symbol!="<td></td>") continue;
            int rowSpan=1, colSpan=1;
            if(symbol=="<td") {
                for(int j=t+1;j<std::min<int>(t+4,tokenShape[1]);++j) {
                    const float* next=scores+static_cast<size_t>(j)*50;
                    int nextId=static_cast<int>(std::max_element(next,next+50)-next);
                    if(nextId<=0 || nextId>=49) break;
                    const auto& value=impl_->tableChars[nextId-1];
                    auto parseSpan=[&](std::string_view prefix,int& span) {
                        if(value.rfind(prefix,0)==0) {
                            auto end=value.find('"',prefix.size());
                            if(end!=std::string::npos) span=std::clamp(std::stoi(value.substr(prefix.size(),end-prefix.size())),1,20);
                        }
                    };
                    parseSpan(" colspan=\"",colSpan); parseSpan(" rowspan=\"",rowSpan);
                    if(value==">") break;
                }
            }
            if(row<0) { row=0; result.rows=1; }
            // Skip columns occupied by a cell from an earlier row.
            auto occupied=[&](int c) { for(const auto& cell:result.cells) if(cell.row<=row && row<cell.row+cell.rowSpan && cell.column<=c && c<cell.column+cell.columnSpan) return true; return false; };
            while(occupied(col)) ++col;
            TableCell cell{row,col,rowSpan,colSpan,{}};
            float minX=1.F,minY=1.F,maxX=0.F,maxY=0.F;
            for(int k=0;k<4;++k) { minX=std::min(minX,boxes[(static_cast<size_t>(t)*8)+2*k]); maxX=std::max(maxX,boxes[(static_cast<size_t>(t)*8)+2*k]); minY=std::min(minY,boxes[(static_cast<size_t>(t)*8)+2*k+1]); maxY=std::max(maxY,boxes[(static_cast<size_t>(t)*8)+2*k+1]); }
            // Coordinates are normalized against the padded 488-square model input.
            const float left=minX*488/scale, right=maxX*488/scale;
            const float top=minY*488/scale, bottom=maxY*488/scale;
            for(const auto& word:words) {
                float cx=word.x+word.width*.5F, cy=word.y+word.height*.5F;
                if(cx>=left && cx<=right && cy>=top && cy<=bottom) {
                    if(!cell.text.empty()) cell.text+=' ';
                    cell.text+=word.text;
                }
            }
            result.cells.push_back(std::move(cell));
            col+=colSpan;
            result.columns=std::max(result.columns,col);
            result.rows=std::max(result.rows,row+rowSpan);
        }
        if(result.cells.empty()) throw std::runtime_error("No table cells were detected. Crop to one table and retry.");
        return true;
    } catch(const std::exception& e) { result={}; error_=e.what(); }
#endif
    return false;
}

std::string tableToCsv(const TableResult& table) {
    if(table.rows<0 || table.columns<0 || table.rows>10000 || table.columns>10000 ||
       static_cast<std::int64_t>(table.rows)*table.columns>1000000) return {};
    std::vector<std::string> grid(static_cast<size_t>(table.rows)*table.columns);
    for(const auto& cell:table.cells) {
        if(cell.row>=0 && cell.row<table.rows && cell.column>=0 && cell.column<table.columns)
            grid[static_cast<size_t>(cell.row)*table.columns+cell.column]=cell.text;
    }
    std::string csv;
    for(int r=0;r<table.rows;++r) {
        for(int c=0;c<table.columns;++c) {
            if(c) csv+=',';
            csv+=csvEscape(grid[static_cast<size_t>(r)*table.columns+c]);
        }
        csv+="\r\n";
    }
    return csv;
}
