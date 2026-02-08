// fleximg DistributorNode Unit Tests
// 分配ノードのテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/common.h"
#include "fleximg/core/types.h"
#include "fleximg/image/image_buffer.h"
#include "fleximg/image/render_types.h"
#include "fleximg/nodes/distributor_node.h"
#include "fleximg/nodes/renderer_node.h"
#include "fleximg/nodes/sink_node.h"
#include "fleximg/nodes/source_node.h"

#include <cmath>

using namespace fleximg;

// =============================================================================
// Helper Functions
// =============================================================================

static ImageBuffer createSolidImage(int width, int height, uint8_t r, uint8_t g,
                                    uint8_t b, uint8_t a) {
  ImageBuffer img(width, height, PixelFormatIDs::RGBA8_Straight);
  ViewPort view = img.view();
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      uint8_t *p = static_cast<uint8_t *>(view.pixelAt(x, y));
      p[0] = r;
      p[1] = g;
      p[2] = b;
      p[3] = a;
    }
  }
  return img;
}

static void getPixelRGBA8(const ViewPort &view, int x, int y, uint8_t &r,
                          uint8_t &g, uint8_t &b, uint8_t &a) {
  const uint8_t *p = static_cast<const uint8_t *>(view.pixelAt(x, y));
  r = p[0];
  g = p[1];
  b = p[2];
  a = p[3];
}

static bool hasNonZeroPixels(const ViewPort &view) {
  for (int y = 0; y < view.height; y++) {
    const uint8_t *row = static_cast<const uint8_t *>(view.pixelAt(0, y));
    for (int x = 0; x < view.width; x++) {
      if (row[x * 4 + 3] > 0)
        return true;
    }
  }
  return false;
}

// =============================================================================
// DistributorNode Construction Tests
// =============================================================================

TEST_CASE("DistributorNode basic construction") {
  SUBCASE("default 1 output") {
    DistributorNode node;
    CHECK(node.outputCount() == 1);
    CHECK(node.name() != nullptr);
  }

  SUBCASE("custom output count") {
    DistributorNode node2(2);
    CHECK(node2.outputCount() == 2);

    DistributorNode node5(5);
    CHECK(node5.outputCount() == 5);
  }
}

TEST_CASE("DistributorNode setOutputCount") {
  DistributorNode node;
  CHECK(node.outputCount() == 1);

  node.setOutputCount(4);
  CHECK(node.outputCount() == 4);

  node.setOutputCount(1);
  CHECK(node.outputCount() == 1);

  // 0以下は1にクランプ
  node.setOutputCount(0);
  CHECK(node.outputCount() == 1);

  node.setOutputCount(-1);
  CHECK(node.outputCount() == 1);
}

// =============================================================================
// DistributorNode Port Tests
// =============================================================================

TEST_CASE("DistributorNode port access") {
  DistributorNode node(3);

  SUBCASE("input port exists") { CHECK(node.inputPort(0) != nullptr); }

  SUBCASE("output ports exist") {
    CHECK(node.outputPort(0) != nullptr);
    CHECK(node.outputPort(1) != nullptr);
    CHECK(node.outputPort(2) != nullptr);
  }
}

// =============================================================================
// DistributorNode Pipeline Tests
// =============================================================================

TEST_CASE("DistributorNode single output pipeline") {
  const int imgSize = 32;
  const int canvasSize = 64;

  ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
  ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);

  SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f),
                 float_to_fixed(imgSize / 2.0f));
  RendererNode renderer;
  DistributorNode distributor(1);
  SinkNode sink(dstImg.view(), float_to_fixed(canvasSize / 2.0f),
                float_to_fixed(canvasSize / 2.0f));

  src >> renderer >> distributor;
  distributor.connectTo(sink, 0, 0);

  renderer.setVirtualScreen(canvasSize, canvasSize);
  renderer.exec();

  // 出力に赤いピクセルがあることを確認
  bool foundRed = false;
  for (int y = 0; y < canvasSize && !foundRed; y++) {
    for (int x = 0; x < canvasSize && !foundRed; x++) {
      uint8_t r, g, b, a;
      getPixelRGBA8(dstImg.view(), x, y, r, g, b, a);
      if (r > 128 && a > 128)
        foundRed = true;
    }
  }
  CHECK(foundRed);
}

TEST_CASE("DistributorNode two outputs receive same image") {
  const int imgSize = 32;
  const int canvasSize = 64;

  ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);
  ImageBuffer dstImg1(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
  ImageBuffer dstImg2(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);

  SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f),
                 float_to_fixed(imgSize / 2.0f));
  RendererNode renderer;
  DistributorNode distributor(2);
  SinkNode sink1(dstImg1.view(), float_to_fixed(canvasSize / 2.0f),
                 float_to_fixed(canvasSize / 2.0f));
  SinkNode sink2(dstImg2.view(), float_to_fixed(canvasSize / 2.0f),
                 float_to_fixed(canvasSize / 2.0f));

  src >> renderer >> distributor;
  distributor.connectTo(sink1, 0, 0);
  distributor.connectTo(sink2, 0, 1);

  renderer.setVirtualScreen(canvasSize, canvasSize);
  renderer.exec();

  // 両方の出力に赤いピクセルがあることを確認
  CHECK(hasNonZeroPixels(dstImg1.view()));
  CHECK(hasNonZeroPixels(dstImg2.view()));

  // 両方の出力が同じ画像であることを確認（フォーマット変換の微差を許容）
  bool match = true;
  for (int y = 0; y < canvasSize && match; y++) {
    for (int x = 0; x < canvasSize && match; x++) {
      uint8_t r1, g1, b1, a1, r2, g2, b2, a2;
      getPixelRGBA8(dstImg1.view(), x, y, r1, g1, b1, a1);
      getPixelRGBA8(dstImg2.view(), x, y, r2, g2, b2, a2);
      int dr = std::abs(static_cast<int>(r1) - static_cast<int>(r2));
      int dg = std::abs(static_cast<int>(g1) - static_cast<int>(g2));
      int db = std::abs(static_cast<int>(b1) - static_cast<int>(b2));
      int da = std::abs(static_cast<int>(a1) - static_cast<int>(a2));
      if (dr > 2 || dg > 2 || db > 2 || da > 2)
        match = false;
    }
  }
  CHECK(match);
}

TEST_CASE("DistributorNode no connected outputs") {
  const int imgSize = 32;
  const int canvasSize = 64;

  ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 255, 0, 0, 255);

  SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f),
                 float_to_fixed(imgSize / 2.0f));
  RendererNode renderer;
  DistributorNode distributor(2); // 2出力だが接続なし

  src >> renderer >> distributor;

  renderer.setVirtualScreen(canvasSize, canvasSize);
  renderer.exec();

  // エラーなく完了すればOK
  CHECK(true);
}

// =============================================================================
// DistributorNode Affine Transform Tests
// =============================================================================

TEST_CASE("DistributorNode affine transform propagation") {
  const int imgSize = 32;
  const int canvasSize = 64;

  ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 0, 255, 0, 255);
  ImageBuffer dstWithAffine(canvasSize, canvasSize,
                            PixelFormatIDs::RGBA8_Straight);
  ImageBuffer dstWithoutAffine(canvasSize, canvasSize,
                               PixelFormatIDs::RGBA8_Straight);

  // アフィン変換あり（平行移動）
  {
    SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f),
                   float_to_fixed(imgSize / 2.0f));
    RendererNode renderer;
    DistributorNode distributor(1);
    distributor.setTranslation(10.0f, 10.0f);
    SinkNode sink(dstWithAffine.view(), float_to_fixed(canvasSize / 2.0f),
                  float_to_fixed(canvasSize / 2.0f));

    src >> renderer >> distributor;
    distributor.connectTo(sink, 0, 0);
    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();
  }

  // アフィン変換なし
  {
    SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f),
                   float_to_fixed(imgSize / 2.0f));
    RendererNode renderer;
    DistributorNode distributor(1);
    SinkNode sink(dstWithoutAffine.view(), float_to_fixed(canvasSize / 2.0f),
                  float_to_fixed(canvasSize / 2.0f));

    src >> renderer >> distributor;
    distributor.connectTo(sink, 0, 0);
    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();
  }

  // 両方とも描画されていること
  CHECK(hasNonZeroPixels(dstWithAffine.view()));
  CHECK(hasNonZeroPixels(dstWithoutAffine.view()));

  // 異なる結果であること（平行移動が適用されている）
  bool identical = true;
  for (int y = 0; y < canvasSize && identical; y++) {
    for (int x = 0; x < canvasSize && identical; x++) {
      uint8_t r1, g1, b1, a1, r2, g2, b2, a2;
      getPixelRGBA8(dstWithAffine.view(), x, y, r1, g1, b1, a1);
      getPixelRGBA8(dstWithoutAffine.view(), x, y, r2, g2, b2, a2);
      if (r1 != r2 || g1 != g2 || b1 != b2 || a1 != a2)
        identical = false;
    }
  }
  CHECK_FALSE(identical);
}

TEST_CASE("DistributorNode three outputs") {
  const int imgSize = 32;
  const int canvasSize = 64;

  ImageBuffer srcImg = createSolidImage(imgSize, imgSize, 0, 0, 255, 255);
  ImageBuffer dstImg1(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
  ImageBuffer dstImg2(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);
  ImageBuffer dstImg3(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);

  SourceNode src(srcImg.view(), float_to_fixed(imgSize / 2.0f),
                 float_to_fixed(imgSize / 2.0f));
  RendererNode renderer;
  DistributorNode distributor(3);
  SinkNode sink1(dstImg1.view(), float_to_fixed(canvasSize / 2.0f),
                 float_to_fixed(canvasSize / 2.0f));
  SinkNode sink2(dstImg2.view(), float_to_fixed(canvasSize / 2.0f),
                 float_to_fixed(canvasSize / 2.0f));
  SinkNode sink3(dstImg3.view(), float_to_fixed(canvasSize / 2.0f),
                 float_to_fixed(canvasSize / 2.0f));

  src >> renderer >> distributor;
  distributor.connectTo(sink1, 0, 0);
  distributor.connectTo(sink2, 0, 1);
  distributor.connectTo(sink3, 0, 2);

  renderer.setVirtualScreen(canvasSize, canvasSize);
  renderer.exec();

  // 3つの出力全てに描画されていること
  CHECK(hasNonZeroPixels(dstImg1.view()));
  CHECK(hasNonZeroPixels(dstImg2.view()));
  CHECK(hasNonZeroPixels(dstImg3.view()));
}
