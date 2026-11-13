/*
 * JungfrauProcessPlugin.cpp
 *
 *  Created on: 30 June 2025
 *      Author: James O'Hea
 */
#include <inttypes.h>
#include <JungfrauProcessPlugin.h>
#include <Json.h>

namespace FrameProcessor
{
    /**
     * Constructor - with member initializer list
     */
    JungfrauProcessPlugin::JungfrauProcessPlugin() : zmq_context_(),
                                                     zmq_socket_(zmq_context_, ZMQ_PULL),
                                                     dropped_frames_(0)
    {
        // zmq config, including high water mark
        int hwm = 10000;
        zmq_socket_.setsockopt(ZMQ_RCVHWM, &hwm, sizeof(hwm));
        add_config_param_metadata(JungfrauProcessPlugin::CONFIG_ENDPOINT, PMDD::STRING_T, PMDA::READ_WRITE);
        add_status_param_metadata(JungfrauProcessPlugin::STATUS_DROPPED_FRAMES, PMDD::UINT_T, PMDA::READ_ONLY, 0);

        LOG4CXX_TRACE(Logger::getLogger("FP.JungfrauProcessPlugin"), "JungfrauProcessPlugin constructor.");
    }

    /**
     * Destructor
     */
    JungfrauProcessPlugin::~JungfrauProcessPlugin()
    {
        rx_thread_.join();
        LOG4CXX_TRACE(Logger::getLogger("FP.JungfrauProcessPlugin"), "JungfrauProcessPlugin destructor.");
    }

    void JungfrauProcessPlugin::process_frame(boost::shared_ptr<Frame> frame)
    {
        LOG4CXX_ERROR(Logger::getLogger("FP.JungfrauProcessPlugin"), "JungfrauProcessPlugin::process_frame should not be called");
    }

    /** Handle configuration requests
     *
     * @param config - Configuration message
     * @param reply - Reply message that will be sent back to client
     */
    void JungfrauProcessPlugin::configure(OdinData::IpcMessage &config, OdinData::IpcMessage &reply)
    {
        if (config.has_param(JungfrauProcessPlugin::CONFIG_ENDPOINT) && this->endpoint_.empty())
        {
            this->endpoint_ = config.get_param<std::string>(JungfrauProcessPlugin::CONFIG_ENDPOINT);

            try
            {
                this->zmq_socket_.connect(this->endpoint_.c_str());
            }
            catch (zmq::error_t &e)
            {
                LOG4CXX_ERROR(Logger::getLogger("FP.JungfrauProcessPlugin"), "Failed to connect to " << this->endpoint_ << ": " << e.what());
                return;
            }

            rx_thread_ = std::thread([this](){ this->handle_rx_socket(); });
        }
    }

    /** Report status
     *
     * @param[out] status - Reference to an IpcMessage value to store the status
     */
    void JungfrauProcessPlugin::status(OdinData::IpcMessage &status)
    {
        status.set_param(this->get_name() + '/' + STATUS_DROPPED_FRAMES, this->dropped_frames_);
    }

    /** Reset dropped frames counter
     *
     * @returns True if reset was successful
     */
    bool JungfrauProcessPlugin::reset_statistics()
    {
        this->dropped_frames_ = 0;
        return true;
    }

    // Listen on ZMQ channel for detector data
    void JungfrauProcessPlugin::handle_rx_socket()
    {
        LOG4CXX_INFO(Logger::getLogger("FP.JungfrauProcessPlugin"), "Connected to " << this->endpoint_ << " - Listening...");

        // items is a structure to describe the socket to be polled
        //
        // First 0 = Polling socket and not file descriptor
        // ZMQ_POLLIN makes it follow readable events
        // Last 0 = Initialise revents (returned events) to zero
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
            LOG4CXX_DEBUG(Logger::getLogger("FP.JungfrauProcessPlugin"), "Received data message");

            // Message is made up of 2 parts
            // - Meta Part
            // - Data Part
            zmq::message_t meta_data_part;
            zmq::message_t data_part;
            this->zmq_socket_.recv(&meta_data_part);
            this->zmq_socket_.recv(&data_part);

            // Push the output data frame
            try {
                boost::shared_ptr<Frame> data_frame = create_data_frame(meta_data_part, data_part);
                LOG4CXX_TRACE(Logger::getLogger("FP.JungfrauProcessPlugin"), "Pushing data frame");
                this->push(std::move(data_frame));
            } catch(std::runtime_error& err) {
                ++this->dropped_frames_;
            }
        }
        LOG4CXX_INFO(Logger::getLogger("FP.JungfrauProcessPlugin"), "Shutting down");
    }

    // Parse the received ZMQ multipart message and extract its contents into a Jungfrau_Message struct
    boost::shared_ptr<Frame> JungfrauProcessPlugin::create_data_frame(zmq::message_t &meta_data_part, zmq::message_t &data_part)
    {
        // Write out the first part of the multipart message (the JSON header) as a string
        // Create a rapidjson document and parse the header
        rapidjson::Document rapidjson_doc;
        rapidjson_doc.Parse( reinterpret_cast<char*>(meta_data_part.data()));
        if (rapidjson_doc.HasParseError())
        {
            throw std::runtime_error("Failed to parse JSON header");
        }

        // Fetch the meta data from the message
        // and stuff it into a FrameMetaData object
        FrameMetaData frame_meta_data;
        frame_meta_data.set_frame_number(rapidjson_doc["frameIndex"].GetUint64());
        frame_meta_data.set_dataset_name("data");
        frame_meta_data.set_data_type(raw_16bit);
        char acq_to_str[21] = {'\0'};
        snprintf(acq_to_str, sizeof(acq_to_str), "%d", rapidjson_doc["acquisition"].GetInt());

        frame_meta_data.set_acquisition_ID(acq_to_str);

        const rapidjson::Value &shape_value = rapidjson_doc["shape"];
        rapidjson::Value::ConstArray shape_array = shape_value.GetArray();

        frame_meta_data.set_dimensions({shape_array[0].GetUint64(), shape_array[1].GetUint64()});
        frame_meta_data.set_compression_type(bslz4);

        // Include the second part of the multipart message (the compressed data)
        size_t compressed_data_size = data_part.size();
        const std::byte *byte_ptr = static_cast<const std::byte *>(data_part.data());

        // Construct a json dict of meta data...
        char to_str[256] = {'\0'};
        snprintf(to_str, sizeof(to_str), "{\"frame_index\":%" PRIu64 ",\"row\":%d,\"column\":%d,\"shape\":[%u,%u],\"bit_mode\":%d,\
            \"exp_length\":%u,\"acquisition\":%s}", rapidjson_doc["frameIndex"].GetUint64(), rapidjson_doc["row"].GetInt(),\
            rapidjson_doc["column"].GetInt(), shape_array[0].GetUint(), shape_array[1].GetUint(), rapidjson_doc["bitmode"].GetInt(), \
            rapidjson_doc["expLength"].GetUint(), acq_to_str);

        // ...and pass it to the Meta Writer
        char header_to_str[42] = {'\0'};
        snprintf(header_to_str, sizeof(header_to_str), "{\"acquisition\":%s}", acq_to_str);
        this->publish_meta(get_name(), "jungfrau-imagedata", to_str, header_to_str);

        // Construct a new data block frame
        boost::shared_ptr<Frame> data_block_frame;
        try {
            data_block_frame = boost::make_shared<DataBlockFrame>(std::move(frame_meta_data), byte_ptr, compressed_data_size, 0);
        } catch(std::runtime_error &e) {
            ++this->dropped_frames_;
        }
        return data_block_frame;
    }

    int JungfrauProcessPlugin::get_version_major()
    {
        return JUNGFRAU_DETECTOR_VERSION_MAJOR;
    }

    int JungfrauProcessPlugin::get_version_minor()
    {
        return JUNGFRAU_DETECTOR_VERSION_MINOR;
    }

    int JungfrauProcessPlugin::get_version_patch()
    {
        return JUNGFRAU_DETECTOR_VERSION_PATCH;
    }

    std::string JungfrauProcessPlugin::get_version_short()
    {
        return JUNGFRAU_DETECTOR_VERSION_STR_SHORT;
    }

    std::string JungfrauProcessPlugin::get_version_long()
    {
        return JUNGFRAU_DETECTOR_VERSION_STR;
    }

} /* namespace FrameProcessor */
