/*
 * JungfrauProcessPlugin.cpp
 *
 *  Created on: 8 May 2017
 *      Author: Matt Taylor
 */

#include <JungfrauProcessPlugin.h>
#include "Json.h"
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

namespace FrameProcessor
{

  const std::string JungfrauProcessPlugin::CONFIG_ENDPOINT = "endpoint";
  const std::string JungfrauProcessPlugin::CONFIG_PERSISTENT_FILES = "persistent_files";

  /**
   * Constuctor - with member initialiser list
   */
  JungfrauProcessPlugin::JungfrauProcessPlugin() : zmq_context_(),
                                                   zmq_socket_(zmq_context_, ZMQ_PULL),
                                                   persistent_files_(false),
                                                   dropped_frames_(0)
  {
    // Setup logging for the class
    logger_ = Logger::getLogger("FP.JungfrauProcessPlugin");
    logger_->setLevel(Level::getAll());

    int hwm = 10000;
    zmq_socket_.setsockopt(ZMQ_RCVHWM, &hwm, sizeof(hwm));

    LOG4CXX_TRACE(logger_, "JungfrauProcessPlugin constructor.");
  }

  /**
   * Destructor
   */
  JungfrauProcessPlugin::~JungfrauProcessPlugin()
  {
    rx_thread_->join();
    rx_thread_.reset();
  }

  /** Handle configuration requests
   *
   * @param config - Configuration message
   * @param reply - Reply message that will be sent back to client
   */
  void JungfrauProcessPlugin::configure(OdinData::IpcMessage &config, OdinData::IpcMessage &reply)
  {
    // Protect this method
    boost::lock_guard<boost::recursive_mutex> lock(mutex_);

    if (config.has_param(JungfrauProcessPlugin::CONFIG_ENDPOINT) && this->endpoint_.empty())
    {
      this->endpoint_ = config.get_param<std::string>(JungfrauProcessPlugin::CONFIG_ENDPOINT);

      try
      {
        this->zmq_socket_.connect(this->endpoint_.c_str());
      }
      catch (zmq::error_t &e)
      {
        LOG4CXX_ERROR(logger_, "Failed to connect to " << this->endpoint_ << ": " << e.what());
        return;
      }

      rx_thread_ = boost::shared_ptr<boost::thread>(
          new boost::thread(boost::bind(&JungfrauProcessPlugin::handle_rx_socket, this)));
    }

    // ####### Needed?
    ''' if (config.has_param(JungfrauProcessPlugin::CONFIG_PERSISTENT_FILES))
    {
      this->persistent_files_ = config.get_param<bool>(JungfrauProcessPlugin::CONFIG_PERSISTENT_FILES);
      if (this->persistent_files_)
      {
        LOG4CXX_INFO(logger_, "Persistent files enabled");
      }
      else
      {
        LOG4CXX_INFO(logger_, "Persistent files disabled");
      }
    }
    '''
  }

  // Parse the received ZMQ multipart message and extract its contents into a Jungfrau_Message struct
  Jungfrau_Message JungfrauProcessPlugin::parse_and_extract(zmq::multipart_t &buffer_multipart_msg)
  {
    // Write out the first part of the multipart message (the JSON header) as a string
    std::string json_header(static_cast<char *>(buffer_multipart_msg.at(0).data()), buffer_multipart_msg.at(0).size());

    // Create a rapidjson document and parse the header
    rapidjson::Document rapidjson_doc;
    rapidjson_doc.Parse(json_header.c_str());
    if (rapidjson_doc.HasParseError())
    {
      throw std::runtime_error("Failed to parse JSON header");
    }

    // Populate the Jungfrau_Message struct
    Jungfrau_Message message;
    message.frame_index = rapidjson_doc["frameIndex"].GetInt();
    message.row = rapidjson_doc["row"].GetInt();
    message.column = rapidjson_doc["column"].GetInt();
    // For each value in the shape array, add it to the shape vector in the message struct
    const rapidjson::Value::ConstArray &shape_array = rapidjson_document["shape"].GetArray();
    for (rapidjson::Value::ConstValueIterator itr = shape_array.Begin(); itr != shape_array.End(); ++itr)
    {
      const rapidjson::Value &shape_value = *itr;
      message.shape.push_back(shape_value.GetInt());
    }
    message.bit_mode = rapidjson_doc["bitmode"].GetInt();
    message.exp_length = rapidjson_doc["expLength"].GetFloat();
    message.acquisition_num = rapidjson_doc["acquisition"].GetInt();

    // Include the second part of the multipart message (the compressed data)
    size_t compressed_data_size = buffer_multipart_msg.at(1).size();
    const std::byte *byte_ptr = static_cast<const std::byte *>(buffer_multipart_msg.at(1).data());
    message.compressed_data.assign(byte_ptr, byte_ptr + compressed_data_size);

    return message;
  }

  // Listen on ZMQ channel for detector data
  void JungfrauProcessPlugin::handle_rx_socket()
  {
    LOG4CXX_INFO(logger_, "Connected to " << this->endpoint_ << " - Listening...");

    // Declare a ZMQ message object to receive a message over a ZMQ socket
    // and a structure to describe the socket to be polled
    // First 0 = Polling socket and not file descriptor
    // ZMQ_POLLIN makes it follow readable events
    // Last 0 = Initialise revents (returned events) to zero
    zmq::multipart_t buffer_multipart_msg;
    zmq::pollitem_t items[] = {{this->zmq_socket_, 0, ZMQ_POLLIN, 0}};

    while (this->isWorking())
    {
      // Poll for 1000ms and skip loop if no messages were received (to check for shutdown)
      zmq::poll(&items[0], 1, 1000);
      if (!(items[0].revents & ZMQ_POLLIN))
      {
        continue;
      }

      // Message found on socket
      buffer_multipart_msg.recv(zmq_socket_);
      Jungfrau_Message message = parse_and_extract(buffer_multipart_msg);
      LOG4CXX_DEBUG_LEVEL(1, logger_, "Received data message");
    }

    LOG4CXX_INFO(logger_, "Shutting down");
  }

  /**
   * Processes a frame
   *
   * \param[in] frame The frame to process
   */
  void JungfrauProcessPlugin::process_frame(boost::shared_ptr<Frame> frame)
  {
    const Jungfrau::FrameHeader *hdrPtr = static_cast<const Jungfrau::FrameHeader *>(frame->get_image_ptr());

    LOG4CXX_TRACE(logger_, "FrameHeader frame currentMessageType: " << hdrPtr->messageType);
    LOG4CXX_TRACE(logger_, "FrameHeader frame series: " << hdrPtr->series);
    LOG4CXX_TRACE(logger_, "FrameHeader frame number: " << hdrPtr->frame_number);
    LOG4CXX_TRACE(logger_, "FrameHeader frame shapeSizeX: " << hdrPtr->shapeSizeX);
    LOG4CXX_TRACE(logger_, "FrameHeader frame shapeSizeY: " << hdrPtr->shapeSizeY);
    LOG4CXX_TRACE(logger_, "FrameHeader frame shapeSizeZ: " << hdrPtr->shapeSizeZ);
    LOG4CXX_TRACE(logger_, "FrameHeader frame startTime: " << hdrPtr->startTime);
    LOG4CXX_TRACE(logger_, "FrameHeader frame stopTime: " << hdrPtr->stopTime);
    LOG4CXX_TRACE(logger_, "FrameHeader frame realTime: " << hdrPtr->realTime);
    LOG4CXX_TRACE(logger_, "FrameHeader frame blob_size: " << hdrPtr->data_size);
    LOG4CXX_TRACE(logger_, "FrameHeader frame data type: " << hdrPtr->dataType);
    LOG4CXX_TRACE(logger_, "FrameHeader frame encoding: " << hdrPtr->encoding);
    LOG4CXX_TRACE(logger_, "FrameHeader frame acquisition ID: " << hdrPtr->acquisitionID);

    // Create status message header

    // Add Acquisition ID
    std::string acqIDString(hdrPtr->acquisitionID);
    OdinData::JsonDict json;
    json.add("acqID", acqIDString);

    if (hdrPtr->messageType == Jungfrau::IMAGE_DATA)
    {
      frame->set_image_offset(sizeof(Jungfrau::FrameHeader));
      frame->set_image_size(hdrPtr->data_size);

      FrameMetaData frame_meta_data;

      frame_meta_data.set_dataset_name("data");

      setFrameEncoding(frame_meta_data, hdrPtr);
      setFrameDataType(frame_meta_data, hdrPtr);
      setFrameDimensions(frame_meta_data, hdrPtr);
      frame_meta_data.set_acquisition_ID(hdrPtr->acquisitionID);

      // Set the compressed_size parameter to the frame size
      frame_meta_data.set_parameter("compressed_size", hdrPtr->data_size);

      frame->set_meta_data(frame_meta_data);
      frame->set_frame_number(hdrPtr->frame_number);

      // Add Frame number
      json.add("frame", hdrPtr->frame_number);

      // Add Series number
      json.add("series", hdrPtr->series);

      // Add Size
      json.add("size", hdrPtr->size_in_header);

      // Add Start Time
      json.add("start_time", hdrPtr->startTime);

      // Add Stop Time
      json.add("stop_time", hdrPtr->stopTime);

      // Add Real Time
      json.add("real_time", hdrPtr->realTime);

      // Add shape
      std::vector<uint32_t> shape;
      shape.push_back(hdrPtr->shapeSizeX);
      shape.push_back(hdrPtr->shapeSizeY);
      json.add("shape", shape);

      // Add data type
      std::string dataTypeString(hdrPtr->dataType);
      json.add("type", dataTypeString);

      // Add encoding
      std::string encodingString(hdrPtr->encoding);
      json.add("encoding", encodingString);

      // Add hash
      std::string hashString(hdrPtr->hash);
      json.add("hash", hashString);

      publish_meta(get_name(), "jungfrau-imagedata", json.str(), json.str());

      this->push(frame);
    }
    else if (hdrPtr->messageType == Jungfrau::IMAGE_APPENDIX)
    {
      std::string dataString((static_cast<const char *>(frame->get_image_ptr()) + sizeof(Jungfrau::FrameHeader)), hdrPtr->data_size);

      // Add Frame number
      json.add("frame", hdrPtr->frame_number);

      publish_meta(get_name(), "jungfrau-imageappendix", dataString, json.str());
    }
    else if (hdrPtr->messageType == Jungfrau::GLOBAL_HEADER_NONE)
    {
      // Add Series number
      json.add("series", hdrPtr->series);

      publish_meta(get_name(), "jungfrau-globalnone", json.str(), json.str());
    }
    else if (hdrPtr->messageType == Jungfrau::GLOBAL_HEADER_CONFIG)
    {
      std::string dataString((static_cast<const char *>(frame->get_image_ptr()) + sizeof(Jungfrau::FrameHeader)), hdrPtr->data_size);

      // Add Series number
      json.add("series", hdrPtr->series);

      publish_meta(get_name(), "jungfrau-globalconfig", dataString, json.str());
    }
    else if (hdrPtr->messageType == Jungfrau::GLOBAL_HEADER_FLATFIELD)
    {
      // Add shape
      std::vector<uint32_t> shape;
      shape.push_back(hdrPtr->shapeSizeX);
      shape.push_back(hdrPtr->shapeSizeY);
      json.add("shape", shape);

      // Add data type
      std::string dataTypeString(hdrPtr->dataType);
      json.add("type", dataTypeString);

      publish_meta(get_name(), "jungfrau-globalflatfield", reinterpret_cast<const void *>(static_cast<const char *>(frame->get_image_ptr()) + sizeof(Jungfrau::FrameHeader)), hdrPtr->data_size, json.str());
    }
    else if (hdrPtr->messageType == Jungfrau::GLOBAL_HEADER_MASK)
    {
      // Add shape
      std::vector<uint32_t> shape;
      shape.push_back(hdrPtr->shapeSizeX);
      shape.push_back(hdrPtr->shapeSizeY);
      json.add("shape", shape);

      // Add data type
      std::string dataTypeString(hdrPtr->dataType);
      json.add("type", dataTypeString);

      publish_meta(get_name(), "jungfrau-globalmask", reinterpret_cast<const void *>(static_cast<const char *>(frame->get_image_ptr()) + sizeof(Jungfrau::FrameHeader)), hdrPtr->data_size, json.str());
    }
    else if (hdrPtr->messageType == Jungfrau::GLOBAL_HEADER_COUNTRATE)
    {
      // Add shape
      std::vector<uint32_t> shape;
      shape.push_back(hdrPtr->shapeSizeX);
      shape.push_back(hdrPtr->shapeSizeY);
      json.add("shape", shape);

      // Add data type
      std::string dataTypeString(hdrPtr->dataType);
      json.add("type", dataTypeString);

      publish_meta(get_name(), "jungfrau-globalcountrate", reinterpret_cast<const void *>(static_cast<const char *>(frame->get_image_ptr()) + sizeof(Jungfrau::FrameHeader)), hdrPtr->data_size, json.str());
    }
    else if (hdrPtr->messageType == Jungfrau::GLOBAL_HEADER_APPENDIX)
    {
      std::string dataString((static_cast<const char *>(frame->get_image_ptr()) + sizeof(Jungfrau::FrameHeader)), hdrPtr->data_size);

      publish_meta(get_name(), "jungfrau-headerappendix", dataString, json.str());
    }
    else if (hdrPtr->messageType == Jungfrau::END_OF_STREAM)
    {
      // Add Series number
      json.add("series", hdrPtr->series);

      publish_meta(get_name(), "jungfrau-end", "", json.str());
    }
  }

  /**
   * Set the encoding on the frame
   *
   * \param[out] frame The frame Meta Data object to set the encoding on
   * \param[in] hdrPtr The header containing the encoding
   */
  void JungfrauProcessPlugin::setFrameEncoding(FrameMetaData &frame, const Jungfrau::FrameHeader *hdrPtr)
  {
    std::string encoding(hdrPtr->encoding);

    // Parse out lz4
    std::size_t found = encoding.find("lz4");
    if (found != std::string::npos)
    {
      found = encoding.find("bs");
      if (found != std::string::npos)
      {
        frame.set_compression_type(bslz4);
      }
      else
      {
        frame.set_compression_type(lz4);
      }
    }
    else
    {
      frame.set_compression_type(no_compression);
    }
  }

  /**
   * Set the data type on the frame
   *
   * \param[out] frame The frame Meta Data object to set the encoding on
   * \param[in] hdrPtr The header containing the encoding
   */
  void JungfrauProcessPlugin::setFrameDataType(FrameMetaData &frame, const Jungfrau::FrameHeader *hdrPtr)
  {
    std::string dataType(hdrPtr->dataType);

    if (dataType.compare("uint8") == 0)
    {
      frame.set_data_type(raw_8bit);
    }
    else if (dataType.compare("uint16") == 0)
    {
      frame.set_data_type(raw_16bit);
    }
    else if (dataType.compare("uint32") == 0)
    {
      frame.set_data_type(raw_32bit);
    }
    else
    {
      LOG4CXX_ERROR(logger_, "Unknown frame data type :" << dataType);
    }
  }

  /**
   * Set the dimensions on the frame
   *
   * \param[out] frame The frame Meta Data object to set the encoding on
   * \param[in] hdrPtr The header containing the dimensions
   */
  void JungfrauProcessPlugin::setFrameDimensions(FrameMetaData &frame, const Jungfrau::FrameHeader *hdrPtr)
  {
    dimensions_t dims;
    if (hdrPtr->shapeSizeZ > 0)
    {
      dims.push_back(hdrPtr->shapeSizeZ);
    }
    dims.push_back(hdrPtr->shapeSizeY);
    dims.push_back(hdrPtr->shapeSizeX);

    frame.set_dimensions(dims);
  }

  int JungfrauProcessPlugin::get_version_major()
  {
    return EIGER_DETECTOR_VERSION_MAJOR;
  }

  int JungfrauProcessPlugin::get_version_minor()
  {
    return EIGER_DETECTOR_VERSION_MINOR;
  }

  int JungfrauProcessPlugin::get_version_patch()
  {
    return EIGER_DETECTOR_VERSION_PATCH;
  }

  std::string JungfrauProcessPlugin::get_version_short()
  {
    return EIGER_DETECTOR_VERSION_STR_SHORT;
  }

  std::string JungfrauProcessPlugin::get_version_long()
  {
    return EIGER_DETECTOR_VERSION_STR;
  }

} /* namespace FrameProcessor */
