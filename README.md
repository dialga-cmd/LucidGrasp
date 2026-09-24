# LucidGrasp

LucidGrasp is a high performance image search engine built to identify and match visual media. It operates by understanding the semantic content of an image rather than relying on basic file hashing or metadata. It uses Contrastive Language Image Pretraining models through ONNX Runtime to generate mathematical embeddings for every image it processes.

### Current Capabilities

At this stage of development the software focuses on local file system analysis. You point it at a directory containing thousands of photos and it will index all of them. You can then provide a target image and the application will instantly retrieve all visually similar files from the indexed folders. The core engine is written in standard C++ and the graphical interface is built with Qt6 ensuring the application runs fast and natively. Please note that this software is built exclusively for Linux and will not function on other platforms.

### Planned Expansion

The long term goal for this project is global discovery. The local search functionality serves as the foundation for a much larger distributed network crawler. Upcoming updates will introduce the ability to scan websites and deep web repositories for specific images. This will turn the application into a powerful asset for cybersecurity professionals and researchers who need to track the spread of sensitive media across the internet.

### Technical Architecture

The codebase is split into a user interface layer and a core processing layer. The core layer handles image decoding and embedding generation. When a file is read the system scales and crops it to the dimensions required by the machine learning model. It then converts the pixel data into a format suitable for ONNX Runtime. The resulting vector is stored in a binary index file for rapid querying. Similarity between images is calculated using cosine distance mathematics.

### Setup Guide

You will need CMake Qt6 and a compiler that supports C++17. Follow these steps to prepare your environment and compile the software.

1. Clone the repository to your local machine.

```bash
git clone https://github.com/dialga-cmd/LucidGrasp.git
cd LucidGrasp
```

2. The software depends on the ONNX Runtime library for machine learning inference. You must download the binaries for your operating system and place them in the correct location. 

```bash
mkdir third_party
cd third_party
wget https://download.onnxruntime.ai/releases/cpu/1.16.1/onnxruntime-linux-x64-1.16.1.tgz
tar xvf onnxruntime-linux-x64-1.16.1.tgz
mv onnxruntime-linux-x64-1.16.1 onnxruntime
cd ..
```

3. You also need a compatible CLIP model in ONNX format to generate the image embeddings. Download the model files and place them in the models directory.

```bash
mkdir models
cd models
wget https://github.com/onnx/models/raw/main/vision/classification/clip/model/clip-vit-base-patch32-224.onnx
cd ..
```

4. Now you can configure the build environment using CMake and compile the executable. 

```bash
mkdir build
cd build
cmake ..
make
```

Once compiled you can launch the application and point it to a folder full of images to begin the initial indexing process.
