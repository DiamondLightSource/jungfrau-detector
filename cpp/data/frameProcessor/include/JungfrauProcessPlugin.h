/*
 * JungfrauProcessPlugin.h
 *
 *  Created on: 30 June 2025
 *      Author: James O'Hea
 */

#ifndef TOOLS_FILEWRITER_JUNGFRAUPROCESSPLUGIN_H_
#define TOOLS_FILEWRITER_JUNGFRAUPROCESSPLUGIN_H_

#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/propertyconfigurator.h>
#include <log4cxx/helpers/exception.h>
using namespace log4cxx;
using namespace log4cxx::helpers;

#include <thread>

#include "zmq/zmq.hpp"

#include "FrameProcessorPlugin.h"
#include "ClassLoader.h"
#include "DataBlockFrame.h"
#include "JungfrauDefinitions.h"
#include <stdint.h>

namespace FrameProcessor
{
  const int NO_COMPRESSION = 0;
  const int LZ4_COMPRESSION = 1;
  const int BSLZ4_COMPRESSION = 2;
  const int UINT8_DATATYPE = 0;
  const int UINT16_DATATYPE = 1;
  const int UINT32_DATATYPE = 2;

  /** Processing of Jungfrau Frame objects.
   *
   * The JungfrauProcessPlugin class is responsible for receiving a raw data
   * zmq messages and parsing the header information. Depending on the frame type, it
   * sends raw image data on down the chain, or sends meta data out to subscribers.
   */
  class JungfrauProcessPlugin : public FrameProcessorPlugin
  {
  public:
    JungfrauProcessPlugin();
    virtual ~JungfrauProcessPlugin();

    int get_version_major();
    int get_version_minor();
    int get_version_patch();
    std::string get_version_short();
    std::string get_version_long();

  private:
    /** Handle data stream socket */
    void handle_rx_socket();

    /** Frames dropped when failing to allocating memory */
    uint64_t dropped_frames_;
    /** Data stream endpoint to connect to */
    std::string endpoint_;
    /** ZeroMQ socket for data stream */
    zmq::socket_t zmq_socket_;
    /** ZeroMQ context */
    zmq::context_t zmq_context_;
    /** Thread for handling data stream socket */
    std::thread rx_thread_;

    static inline const std::string CONFIG_ENDPOINT = "endpoint";
    static inline const std::string STATUS_DROPPED_FRAMES = "dropped_frames";

    boost::shared_ptr<Frame> create_data_frame(zmq::message_t &meta_data_part, zmq::message_t &data_part);

    /** Parent class methods */
    void configure(OdinData::IpcMessage &config, OdinData::IpcMessage &reply);
    void status(OdinData::IpcMessage &status);
    bool reset_statistics();
    /** `process_frame` should not be called as the data is received directly from a socket */
    void process_frame(boost::shared_ptr<Frame> frame);
  };

  /**
   * Registration of this plugin through the ClassLoader.  This macro
   * registers the class without needing to worry about name mangling
   */
  REGISTER(FrameProcessorPlugin, JungfrauProcessPlugin, "JungfrauProcessPlugin");

} /* namespace FrameProcessor */

#endif /* TOOLS_FILEWRITER_JUNGFRAUPROCESSPLUGIN_H_ */
