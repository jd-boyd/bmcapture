from setuptools import setup, Extension, find_packages
import os
import numpy as np

# Find macOS SDK path
sdk_path = os.popen('xcrun --show-sdk-path').read().strip()

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
    extra_compile_args=[
        '-std=c++11',
        '-g',
        '-Wno-unused-function',
        '-Wno-deprecated-declarations',
        f'-isysroot{sdk_path}'
    ],
    extra_link_args=[
        '-framework', 'CoreFoundation',
        '-framework', 'CoreVideo',
        '-framework', 'CoreMedia'
    ],
    language='c++'
)

setup(
    ext_modules=[bmcapture_c_module],
)
