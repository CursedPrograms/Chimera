// Chimera - the Quick Selection brush, ported from PhotoCraft (github.com/storytold/photocraft,
// crates/algo/src/segment/quick.rs and segment.rs, MIT OR Apache-2.0): Photoshop's behaviour, measured by PhotoCraft as
// a black box and reproduced by one min cut (Boykov & Jolly 2001; max-flow by Boykov & Kolmogorov 2004):
//   sum over selected pixels beyond the brush footprint of  AREA * (1 + DIST * d^POW)
//   + sum over cut neighbour pairs p~q of  (FLOOR + 1 / (1 + (|Ip - Iq| / SIGMA)^K)) / |p - q|
// with the (slightly grown) footprint always selected and d the distance from it. Big windows are cut coarse to fine
// (Lombaert et al. 2005). Also the exact distance transform (Felzenszwalb & Huttenlocher) the cut uses.
#pragma once
#include "doc.h"
#include <cstdint>
#include <vector>

namespace QuickSel {

// a stroke's points (x, y pairs, canvas pixels) with a brush `size` across, on `img` (canvas-sized RGBA): the region
// it selects, canvas-sized 0..255 (empty: the stroke missed the canvas)
std::vector<uint8_t> select(const Image& img, const std::vector<float>& points, float size);

// the Euclidean distance of every pixel from the nearest `true` one (exact)
std::vector<float> edt(const std::vector<uint8_t>& seeds, int w, int h);

}
