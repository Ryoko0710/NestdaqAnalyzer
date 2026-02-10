# ------------------------------------------------------------------------------
# OnlineAnalysisSlicer
# ------------------------------------------------------------------------------
add_executable(OnlineAnalysisSlicer
    OnlineAnalysisSlicer.cxx
)

target_link_libraries(OnlineAnalysisSlicer
    NestdaqTFSlicer
    FairMQ::FairMQ
    FairMQStateMachine
    FairLogger::FairLogger
    ${ROOT_LIBRARIES}
    ${Boost_LIBRARIES}
)

# ------------------------------------------------------------------------------
# OnlineAnalysisTFBuilder
# ------------------------------------------------------------------------------
# NestdaqUnpacker does not seem to export a library.
# We compile its sources directly here.
set(NESTDAQ_UNPACKER_SRC_DIR "/home/unidaq/repos/NestdaqUnpacker/src")

add_executable(OnlineAnalysisTFBuilder
    OnlineAnalysisTFBuilder.cxx
    ${NESTDAQ_UNPACKER_SRC_DIR}/NodeUnpacker.cc
    ${NESTDAQ_UNPACKER_SRC_DIR}/LeafProcessor.cc
    ${NESTDAQ_UNPACKER_SRC_DIR}/LeafDecoder.cc
    ${NESTDAQ_UNPACKER_SRC_DIR}/NodeHeaderDecoder.cc
)

target_link_libraries(OnlineAnalysisTFBuilder
    NestdaqTFSlicer
    FairMQ::FairMQ
    FairMQStateMachine
    FairLogger::FairLogger
    ${ROOT_LIBRARIES}
    ${Boost_LIBRARIES}
)

install(TARGETS OnlineAnalysisSlicer OnlineAnalysisTFBuilder RUNTIME DESTINATION bin)
