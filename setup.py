from setuptools import setup, Extension, find_packages
import os
import sys
import numpy as np

# Platform-specific configuration
is_macos = sys.platform == 'darwin'
extra_compile_args = ['-std=c++11', '-g', '-Wno-unused-function', '-Wno-deprecated-declarations']
extra_link_args = []

# macOS-specific settings
if is_macos:
    # Find macOS SDK path
    sdk_path = os.popen('xcrun --show-sdk-path').read().strip()
    if sdk_path:
        extra_compile_args.append(f'-isysroot{sdk_path}')
    
    # Add macOS frameworks
    extra_link_args.extend([
        '-framework', 'CoreFoundation',
        '-framework', 'CoreVideo',
        '-framework', 'CoreMedia'
    ])

# Define extension module
bmcapture_c_module = Extension(
    'bmcapture_c',
    sources=[
        'src/bmcapture_python.cpp',
        'src/bmcapture.cpp',
        'libs/DeckLink/src/DeckLinkAPIDispatch.cpp'
    ],
    include_dirs=[
        np.get_include(),
        'libs/DeckLink/include',
        '.'
    ],
    extra_compile_args=extra_compile_args,
    extra_link_args=extra_link_args,
    language='c++'
)

setup(
    ext_modules=[bmcapture_c_module],
)
