#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct RecognitionImage {
    int width = 0;
    int height = 0;
    // Packed RGB, eight bits per channel.
    std::vector<std::uint8_t> rgb;
};

struct TextRegion {
    int x = 0, y = 0, width = 0, height = 0;
    std::string text;
    float confidence = 0;
};

struct TableCell {
    int row = 0, column = 0, rowSpan = 1, columnSpan = 1;
    std::string text;
};

struct TableResult {
    int rows = 0, columns = 0;
    std::vector<TableCell> cells;
};

class Recognizer final {
public:
    explicit Recognizer(std::filesystem::path modelDirectory);
    ~Recognizer();
    Recognizer(const Recognizer&) = delete;
    Recognizer& operator=(const Recognizer&) = delete;

    bool load();
    bool recognizeText(const RecognitionImage& image, std::vector<TextRegion>& result);
    bool recognizeTable(const RecognitionImage& image, TableResult& result);
    const std::string& lastError() const noexcept;
    bool isLoaded() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::filesystem::path directory_;
    std::string error_;
};

std::string tableToCsv(const TableResult& table);
