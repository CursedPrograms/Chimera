// LayerTack - Select Subject / Remove Background: U2-Net-p (a small salient-object model, ~5 MB) through
// ONNX Runtime, loaded at run time (onnxruntime.dll next to the exe). The model is fetched once, on first use,
// into F:\AIModels\segment when that drive is there (the models folder), else into LayerTack's settings folder.
#pragma once
#include "doc.h"
#include <string>
#include <vector>

// a canvas-sized 0..255 map of the main subject of `img`
bool segmentSubject(const Image& img, std::vector<uint8_t>& out, std::string& err);
std::string segmentModelPath();
