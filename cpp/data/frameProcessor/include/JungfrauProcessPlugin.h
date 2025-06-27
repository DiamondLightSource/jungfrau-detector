/*
 * JungfrauProcessPlugin.h
 *
 *  Created on: 8 May 2017
 *      Author: Matt Taylor
 */

#ifndef TOOLS_FILEWRITER_JUNGFRAUPROCESSPLUGIN_H_
#define TOOLS_FILEWRITER_JUNGFRAUPROCESSPLUGIN_H_

#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/propertyconfigurator.h>
#include <log4cxx/helpers/exception.h>
using namespace log4cxx;
using namespace log4cxx::helpers;

#include "zmq/zmq.hpp"

#include "FrameProcessorPlugin.h"
#include "ClassLoader.h"
#include "JungfrauDefinitions.h"
#include <stdint.h>

struct Jungfrau_Message
{
  int frame_index;
  int row;
  int column;
  std::vector<int> shape; // Assuming shape is an array of ints
  int bit_mode;
  float exp_length;
  int acquisition_num;
  std::vector<std::byte> compressed_data;
};

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
   * Frame object and parsing the header information. Depending on the frame type, it
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
    void process_frame(boost::shared_ptr<Frame> frame);
    // void setFrameEncoding(FrameMetaData &frame, const Jungfrau::FrameHeader *hdrPtr);
    // void setFrameDataType(FrameMetaData &frame, const Jungfrau::FrameHeader *hdrPtr);
    // void setFrameDimensions(FrameMetaData &frame, const Jungfrau::FrameHeader *hdrPtr);
    /** Data stream endpoint to connect to */
    std::string endpoint_;
    /** ZeroMQ context */
    zmq::context_t zmq_context_;
    /** ZeroMQ socket for data stream */
    zmq::socket_t zmq_socket_;
    /** Thread for handling data stream socket */
    boost::shared_ptr<boost::thread> rx_thread_;
    /** Mutex used to make this class thread safe */
    boost::recursive_mutex mutex_;
    /** Pointer to logger */
    LoggerPtr logger_;
  };

  /**
   * Registration of this plugin through the ClassLoader.  This macro
   * registers the class without needing to worry about name mangling
   */
  REGISTER(FrameProcessorPlugin, JungfrauProcessPlugin, "JungfrauProcessPlugin");

} /* namespace FrameProcessor */

#endif /* TOOLS_FILEWRITER_JUNGFRAUPROCESSPLUGIN_H_ */
