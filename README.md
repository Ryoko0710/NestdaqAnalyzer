# Nestdaq Analyzer

Online analysis tools for Nestdaq data acquisition system.

**現在開発段階です (Currently in development stage)**

## Project Structure

*   **NestdaqTFSlicer**: Library for unpacking TimeFrames and slicing data. Contains `TFSlicerUnpacker` and `UnpackTdc`.
*   **analyzer**: Executables for online analysis.
    *   `OnlineAnalysisSlicer`: Reads TimeFrames, slices them based on triggers, and creates histograms.
    *   `OnlineAnalysisTFBuilder`: Online Event Builder and Analysis Node.

## Build Instructions

```bash
mkdir build
cd build
cmake ..
make -j4
```

## Dependencies
*   FairMQ
*   ROOT
*   Boost
*   NestdaqUnpacker (expected at `/home/unidaq/repos/NestdaqUnpacker`)