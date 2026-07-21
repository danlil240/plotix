# Homebrew formula for Spectra
# Install: brew install spectra (after tap setup)
# Or:      brew install --build-from-source spectra

class Spectra < Formula
  desc "GPU-accelerated scientific plotting library"
  homepage "https://github.com/danlil240/Spectra"
  url "https://github.com/danlil240/Spectra/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "PLACEHOLDER_SHA256"
  license "MIT"
  head "https://github.com/danlil240/Spectra.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "glslang" => :build
  depends_on "vulkan-headers" => :build
  depends_on "glfw"
  depends_on "molten-vk"
  depends_on "vulkan-loader"
  depends_on "qt@6"

  def install
    args = %W[
      -DCMAKE_BUILD_TYPE=Release
      -DSPECTRA_BUILD_EXAMPLES=OFF
      -DSPECTRA_BUILD_TESTS=OFF
      -DSPECTRA_BUILD_GOLDEN_TESTS=OFF
      -DSPECTRA_USE_QT=ON
      -DSPECTRA_BUILD_QT_APP=ON
    ]

    system "cmake", "-B", "build", *std_cmake_args, *args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/spectra-backend --version")
  end
end
