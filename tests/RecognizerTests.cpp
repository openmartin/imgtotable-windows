#include "core/Recognizer.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>

static void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
static RecognitionImage loadFixture(const char* path) {
 std::ifstream fixture(path,std::ios::binary);
 std::string magic;int width=0,height=0,maximum=0;
 fixture>>magic>>width>>height>>maximum;
 require(magic=="P5" && width>0 && height>0 && maximum==255,"Bad image fixture");
 fixture.get();
 std::vector<unsigned char> gray(static_cast<size_t>(width)*height);
 fixture.read(reinterpret_cast<char*>(gray.data()),static_cast<std::streamsize>(gray.size()));
 require(fixture.gcount()==static_cast<std::streamsize>(gray.size()),"Truncated image fixture");
 RecognitionImage sample{width,height,std::vector<std::uint8_t>(gray.size()*3)};
 for(size_t i=0;i<gray.size();++i)for(int c=0;c<3;++c)sample.rgb[i*3+c]=gray[i];
 return sample;
}
int main(int argc,char** argv) {
 try {
  TableResult t{2,2,{{0,0,1,1,"a,b"},{0,1,1,1,"say \"hi\""},{1,0,1,1,"line\nbreak"},{1,1,1,1,"中文"}}};
  require(tableToCsv(t)=="\"a,b\",\"say \"\"hi\"\"\"\r\n\"line\nbreak\",中文\r\n","CSV escaping failed");
  Recognizer missing("/nonexistent/imgtotable-models");
  require(!missing.load() && !missing.lastError().empty(),"Missing models need an error");
  if(argc==5) {
   Recognizer real(argv[1]);
   require(real.load(),real.lastError().c_str());
   RecognitionImage bad;
   std::vector<TextRegion> words{{0,0,1,1,"stale",1}};
   require(!real.recognizeText(bad,words) && words.empty(),"Invalid image must clear OCR result");
   RecognitionImage white{320,160,std::vector<std::uint8_t>(320*160*3,255)};
   require(real.recognizeText(white,words),real.lastError().c_str());
   auto sample=loadFixture(argv[2]);
   require(real.recognizeText(sample,words),real.lastError().c_str());
   require(words.size()==4 && words[0].text=="Name" && words[1].text=="Score" &&
           words[2].text=="Alice" && words[3].text=="42","Text decoding differs on real model");
   auto longLine=loadFixture(argv[4]);
   require(real.recognizeText(longLine,words),real.lastError().c_str());
   require(words.size()==1 && words[0].text==
           "为了验证中文识别准确率我们用一段较长的句子测试截图工具的实际效果",
           "Long Chinese line was distorted during recognition");
   TableResult table;
   require(real.recognizeTable(sample,table),real.lastError().c_str());
   require(table.rows==2 && table.columns==2 && table.cells.size()==4,"Table dimensions differ");
   require(tableToCsv(table)=="Name,Score\r\nAlice,42\r\n","Table OCR matching differs");
   sample=loadFixture(argv[3]);
   require(real.recognizeTable(sample,table),real.lastError().c_str());
   require(table.rows==2 && table.columns==2 && table.cells.size()==3,"Merged table dimensions differ");
   require(table.cells[0].columnSpan==2 && table.cells[0].text=="Name","Merged cell not decoded");
   require(tableToCsv(table)=="Name,\r\nAlice,42\r\n","Merged table CSV differs");
  }
  std::cout<<"Recognizer tests passed\n";
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
