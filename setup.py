from setuptools import setup, Extension
import os
import numpy as np

# Find macOS SDK path
sdk_path = os.popen('xcrun --show-sdk-path').read().strip()

# Define extension module
bmcapture_module = Extension(
    'bmcapture',
    sources=[
        'python/bmcapture_python.cpp',
        'bmcapture.cpp',
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
    name='bmcapture',
    version='0.1.0',
    description='Python wrapper for BlackMagic capture devices',
    ext_modules=[bmcapture_module],
)
