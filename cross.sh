#!/bin/bash

PACKAGES=(
  mingw-w64-SDL3
  mingw-w64-SDL3_image
  mingw-w64-zlib
  mingw-w64-cmake
  mingw-w64-make
  mingw-w64-gcc
  mingw-w64-vulkan-headers
  mingw-w64-vulkan-icd-loader
  mingw-w64-vulkan-validation-layers  # optional
)

install_with_pacman()
{
  sudo pacman -S --needed --noconfirm "${PACKAGES[@]}"
}

install_with_yay()
{
  yay -S --needed --noconfirm "${PACKAGES[@]}"
}

install_with_apt()
{
  echo "mingw packages are not directly available in Debian/Ubuntu repos."
  echo "Try using MXE (https://mxe.cc/) or manually install cross-tools."
  exit 1
}

install_with_dnf()
{
  sudo dnf install -y mingw64-gcc mingw64-SDL3 mingw64-SDL3_image mingw64-zlib
  echo "You will need to install the vulkan packages on fedora yourself."
}

install_with_zypper()
{
  sudo zypper install -y mingw64-gcc mingw64-SDL3 mingw64-SDL3_image mingw64-zlib
}

if command -v yay >/dev/null; then
  install_with_yay
elif command -v pacman >/dev/null; then
  install_with_pacman
elif command -v apt >/dev/null; then
  install_with_apt
elif command -v dnf >/dev/null; then
  install_with_dnf
elif command -v zypper >/dev/null; then
  install_with_zypper
else
  echo "No supported package manager found!"
  echo "Please install the following packages:"
  for pkg in "${PACKAGES[@]}"; do
    echo "    $pkg"
  done;
  exit 1
fi

mkdir -p build
cd build ; $ x86_64-w64-mingw32-cmake -DCMAKE_TOOLCHAIN_FILE=../mingwtoolchain.cmake -DVulkan_INCLUDE_DIR=/usr/x86_64-w64-mingw32/include -DVulkan_LIBRARY=/usr/x86_64-w64-mingw32/lib/libvulkan-1.a .. ; x86_64-w64-mingw32-make -j16