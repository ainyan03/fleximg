// fleximg NinePatchSourceNode Unit Tests
// 9patch画像ソースノードのテスト

#include "doctest.h"

#define FLEXIMG_NAMESPACE fleximg
#include "fleximg/core/common.h"
#include "fleximg/core/types.h"
#include "fleximg/image/image_buffer.h"
#include "fleximg/image/render_types.h"
#include "fleximg/nodes/ninepatch_source_node.h"
#include "fleximg/nodes/renderer_node.h"
#include "fleximg/nodes/sink_node.h"

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

// 9patch互換画像を作成（外周1pxがメタデータ）
// 内部画像は単色、上辺と左辺に黒ピクセルで伸縮領域を指定
static ImageBuffer createNinePatchImage(int innerWidth, int innerHeight,
                                        int stretchXStart, int stretchXEnd,
                                        int stretchYStart, int stretchYEnd,
                                        uint8_t r, uint8_t g, uint8_t b) {
  int totalW = innerWidth + 2;
  int totalH = innerHeight + 2;
  ImageBuffer img(totalW, totalH, PixelFormatIDs::RGBA8_Straight);
  ViewPort view = img.view();

  // 全体を透明で初期化
  for (int y = 0; y < totalH; y++) {
    for (int x = 0; x < totalW; x++) {
      uint8_t *p = static_cast<uint8_t *>(view.pixelAt(x, y));
      p[0] = 0;
      p[1] = 0;
      p[2] = 0;
      p[3] = 0;
    }
  }

  // 内部画像を塗る（不透明）
  for (int y = 1; y <= innerHeight; y++) {
    for (int x = 1; x <= innerWidth; x++) {
      uint8_t *p = static_cast<uint8_t *>(view.pixelAt(x, y));
      p[0] = r;
      p[1] = g;
      p[2] = b;
      p[3] = 255;
    }
  }

  // 上辺メタデータ（y=0）: 横方向伸縮領域を黒ピクセルで指定
  for (int x = stretchXStart; x <= stretchXEnd; x++) {
    uint8_t *p = static_cast<uint8_t *>(view.pixelAt(x + 1, 0));
    p[0] = 0;
    p[1] = 0;
    p[2] = 0;
    p[3] = 255; // 黒不透明 = 伸縮マーカー
  }

  // 左辺メタデータ（x=0）: 縦方向伸縮領域を黒ピクセルで指定
  for (int y = stretchYStart; y <= stretchYEnd; y++) {
    uint8_t *p = static_cast<uint8_t *>(view.pixelAt(0, y + 1));
    p[0] = 0;
    p[1] = 0;
    p[2] = 0;
    p[3] = 255; // 黒不透明 = 伸縮マーカー
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
// NinePatchSourceNode Construction Tests
// =============================================================================

TEST_CASE("NinePatchSourceNode basic construction") {
  NinePatchSourceNode node;
  CHECK(node.name() != nullptr);
  CHECK(node.outputWidth() == doctest::Approx(0.0f));
  CHECK(node.outputHeight() == doctest::Approx(0.0f));
}

// =============================================================================
// NinePatchSourceNode setupWithBounds Tests
// =============================================================================

TEST_CASE("NinePatchSourceNode setupWithBounds") {
  NinePatchSourceNode node;

  // 20x20 の画像、左右上下各5pxの固定部
  ImageBuffer srcImg = createSolidImage(20, 20, 128, 128, 128, 255);

  node.setupWithBounds(srcImg.view(), 5, 5, 5, 5);

  CHECK(node.srcLeft() == 5);
  CHECK(node.srcTop() == 5);
  CHECK(node.srcRight() == 5);
  CHECK(node.srcBottom() == 5);
}

TEST_CASE("NinePatchSourceNode setupWithBounds asymmetric") {
  NinePatchSourceNode node;

  ImageBuffer srcImg = createSolidImage(30, 20, 128, 128, 128, 255);

  node.setupWithBounds(srcImg.view(), 3, 4, 7, 6);

  CHECK(node.srcLeft() == 3);
  CHECK(node.srcTop() == 4);
  CHECK(node.srcRight() == 7);
  CHECK(node.srcBottom() == 6);
}

// =============================================================================
// NinePatchSourceNode setupFromNinePatch Tests
// =============================================================================

TEST_CASE("NinePatchSourceNode setupFromNinePatch") {
  // 10x10 の内部画像、伸縮領域は [3,6] × [3,6]
  // → left=3, right=10-1-6=3, top=3, bottom=10-1-6=3
  ImageBuffer ninePatch =
      createNinePatchImage(10, 10, 3, 6, 3, 6, 200, 200, 200);

  NinePatchSourceNode node;
  node.setupFromNinePatch(ninePatch.view());

  CHECK(node.srcLeft() == 3);
  CHECK(node.srcTop() == 3);
  CHECK(node.srcRight() == 3);
  CHECK(node.srcBottom() == 3);
}

TEST_CASE("NinePatchSourceNode setupFromNinePatch asymmetric") {
  // 12x10 の内部画像、伸縮領域は横[2,9]、縦[3,7]
  // → left=2, right=12-1-9=2, top=3, bottom=10-1-7=2
  ImageBuffer ninePatch =
      createNinePatchImage(12, 10, 2, 9, 3, 7, 200, 200, 200);

  NinePatchSourceNode node;
  node.setupFromNinePatch(ninePatch.view());

  CHECK(node.srcLeft() == 2);
  CHECK(node.srcRight() == 2);
  CHECK(node.srcTop() == 3);
  CHECK(node.srcBottom() == 2);
}

TEST_CASE("NinePatchSourceNode setupFromNinePatch invalid image") {
  SUBCASE("too small") {
    // 2x2 は最小サイズ(3x3)未満
    ImageBuffer tinyImg(2, 2, PixelFormatIDs::RGBA8_Straight);
    NinePatchSourceNode node;
    node.setupFromNinePatch(tinyImg.view());
    // sourceValid_がfalseになるため、パイプラインは空結果を返す
    // (直接アクセスする方法がないため、パイプライン実行で確認)
  }
}

// =============================================================================
// NinePatchSourceNode Output Size Tests
// =============================================================================

TEST_CASE("NinePatchSourceNode setOutputSize") {
  NinePatchSourceNode node;
  node.setOutputSize(100.0f, 50.0f);

  CHECK(node.outputWidth() == doctest::Approx(100.0f));
  CHECK(node.outputHeight() == doctest::Approx(50.0f));
}

TEST_CASE("NinePatchSourceNode setPivot") {
  NinePatchSourceNode node;
  int_fixed px = float_to_fixed(10.0f);
  int_fixed py = float_to_fixed(20.0f);
  node.setPivot(px, py);

  CHECK(node.pivotX() == px);
  CHECK(node.pivotY() == py);
}

// =============================================================================
// NinePatchSourceNode Pipeline Tests
// =============================================================================

TEST_CASE("NinePatchSourceNode basic pipeline") {
  // 内部画像: 20x20, 固定部: 各5px
  ImageBuffer srcImg = createSolidImage(20, 20, 255, 0, 0, 255);

  const int canvasSize = 40;
  ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);

  NinePatchSourceNode ninePatch;
  ninePatch.setupWithBounds(srcImg.view(), 5, 5, 5, 5);
  ninePatch.setOutputSize(40.0f, 40.0f); // 20→40に拡大

  RendererNode renderer;
  SinkNode sink(dstImg.view(), 0, 0);

  ninePatch >> renderer >> sink;
  renderer.setVirtualScreen(canvasSize, canvasSize);
  renderer.exec();

  // 出力に描画されていること
  CHECK(hasNonZeroPixels(dstImg.view()));
}

TEST_CASE("NinePatchSourceNode same size output") {
  // 出力サイズ = ソースサイズ → スケール1.0
  ImageBuffer srcImg = createSolidImage(20, 20, 0, 255, 0, 255);

  const int canvasSize = 20;
  ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);

  NinePatchSourceNode ninePatch;
  ninePatch.setupWithBounds(srcImg.view(), 5, 5, 5, 5);
  ninePatch.setOutputSize(20.0f, 20.0f);

  RendererNode renderer;
  SinkNode sink(dstImg.view(), 0, 0);

  ninePatch >> renderer >> sink;
  renderer.setVirtualScreen(canvasSize, canvasSize);
  renderer.exec();

  CHECK(hasNonZeroPixels(dstImg.view()));
}

TEST_CASE("NinePatchSourceNode stretch only horizontally") {
  // 横のみ拡大
  ImageBuffer srcImg = createSolidImage(20, 20, 0, 0, 255, 255);

  const int canvasW = 60;
  const int canvasH = 20;
  ImageBuffer dstImg(canvasW, canvasH, PixelFormatIDs::RGBA8_Straight);

  NinePatchSourceNode ninePatch;
  ninePatch.setupWithBounds(srcImg.view(), 5, 5, 5, 5);
  ninePatch.setOutputSize(60.0f, 20.0f);

  RendererNode renderer;
  SinkNode sink(dstImg.view(), 0, 0);

  ninePatch >> renderer >> sink;
  renderer.setVirtualScreen(canvasW, canvasH);
  renderer.exec();

  CHECK(hasNonZeroPixels(dstImg.view()));
}

TEST_CASE("NinePatchSourceNode output smaller than fixed parts") {
  // 出力サイズが固定部合計(10)より小さい場合のクリッピング
  ImageBuffer srcImg = createSolidImage(20, 20, 255, 255, 0, 255);

  const int canvasSize = 8;
  ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);

  NinePatchSourceNode ninePatch;
  ninePatch.setupWithBounds(srcImg.view(), 5, 5, 5, 5);
  ninePatch.setOutputSize(8.0f, 8.0f); // 固定部合計10より小さい

  RendererNode renderer;
  SinkNode sink(dstImg.view(), 0, 0);

  ninePatch >> renderer >> sink;
  renderer.setVirtualScreen(canvasSize, canvasSize);
  renderer.exec();

  // クリッピングされても描画されること
  CHECK(hasNonZeroPixels(dstImg.view()));
}

TEST_CASE("NinePatchSourceNode from 9patch image pipeline") {
  // setupFromNinePatch を使ったパイプラインテスト
  ImageBuffer ninePatchImg =
      createNinePatchImage(16, 16, 4, 11, 4, 11, 100, 150, 200);

  const int canvasSize = 48;
  ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight);

  NinePatchSourceNode ninePatch;
  ninePatch.setupFromNinePatch(ninePatchImg.view());
  ninePatch.setOutputSize(48.0f, 48.0f);

  RendererNode renderer;
  SinkNode sink(dstImg.view(), 0, 0);

  ninePatch >> renderer >> sink;
  renderer.setVirtualScreen(canvasSize, canvasSize);
  renderer.exec();

  CHECK(hasNonZeroPixels(dstImg.view()));
}

TEST_CASE("NinePatchSourceNode with position offset") {
  ImageBuffer srcImg = createSolidImage(20, 20, 255, 0, 0, 255);

  const int canvasSize = 60;
  ImageBuffer dstNoOffset(canvasSize, canvasSize,
                          PixelFormatIDs::RGBA8_Straight);
  ImageBuffer dstWithOffset(canvasSize, canvasSize,
                            PixelFormatIDs::RGBA8_Straight);

  // オフセットなし
  {
    NinePatchSourceNode ninePatch;
    ninePatch.setupWithBounds(srcImg.view(), 5, 5, 5, 5);
    ninePatch.setOutputSize(30.0f, 30.0f);
    RendererNode renderer;
    SinkNode sink(dstNoOffset.view(), 0, 0);
    ninePatch >> renderer >> sink;
    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();
  }

  // オフセットあり
  {
    NinePatchSourceNode ninePatch;
    ninePatch.setupWithBounds(srcImg.view(), 5, 5, 5, 5);
    ninePatch.setOutputSize(30.0f, 30.0f);
    ninePatch.setPosition(10.0f, 10.0f);
    RendererNode renderer;
    SinkNode sink(dstWithOffset.view(), 0, 0);
    ninePatch >> renderer >> sink;
    renderer.setVirtualScreen(canvasSize, canvasSize);
    renderer.exec();
  }

  CHECK(hasNonZeroPixels(dstNoOffset.view()));
  CHECK(hasNonZeroPixels(dstWithOffset.view()));

  // 位置が異なること
  bool identical = true;
  for (int y = 0; y < canvasSize && identical; y++) {
    for (int x = 0; x < canvasSize && identical; x++) {
      uint8_t r1, g1, b1, a1, r2, g2, b2, a2;
      getPixelRGBA8(dstNoOffset.view(), x, y, r1, g1, b1, a1);
      getPixelRGBA8(dstWithOffset.view(), x, y, r2, g2, b2, a2);
      if (r1 != r2 || g1 != g2 || b1 != b2 || a1 != a2)
        identical = false;
    }
  }
  CHECK_FALSE(identical);
}

TEST_CASE("NinePatchSourceNode invalid source produces empty output") {
  const int canvasSize = 32;
  // ゼロ初期化して未描画の検出を可能にする
  ImageBuffer dstImg(canvasSize, canvasSize, PixelFormatIDs::RGBA8_Straight,
                     InitPolicy::Zero);

  NinePatchSourceNode ninePatch;
  // setupしない → sourceValid_ = false
  ninePatch.setOutputSize(32.0f, 32.0f);

  RendererNode renderer;
  SinkNode sink(dstImg.view(), 0, 0);

  ninePatch >> renderer >> sink;
  renderer.setVirtualScreen(canvasSize, canvasSize);
  renderer.exec();

  // 何も描画されない
  CHECK_FALSE(hasNonZeroPixels(dstImg.view()));
}
