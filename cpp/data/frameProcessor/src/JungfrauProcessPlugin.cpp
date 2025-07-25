/*
 * JungfrauProcessPlugin.cpp
 *
 *  Created on: 30 June 2025
 *      Author: James O'Hea
 */

#include <JungfrauProcessPlugin.h>
#include <Json.h>

namespace FrameProcessor
{

    const std::string JungfrauProcessPlugin::CONFIG_ENDPOINT = "endpoint";
    // const std::string JungfrauProcessPlugin::CONFIG_PERSISTENT_FILES = "persistent_files";

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

        // zmq config, including high water mark
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
        LOG4CXX_TRACE(logger_, "ExcaliburProcessPlugin destructor.");
    }

    void JungfrauProcessPlugin::process_frame(boost::shared_ptr<Frame> frame)
    {
        LOG4CXX_ERROR(logger_, "JungfrauProcessPlugin::process_frame should not be called");
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
    }

    // Listen on ZMQ channel for detector data
    void JungfrauProcessPlugin::handle_rx_socket()
    {
        LOG4CXX_INFO(logger_, "Connected to " << this->endpoint_ << " - Listening...");

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
            LOG4CXX_DEBUG(logger_, "Received data message");

            // Message is made up of 2 parts
            // - Meta Part
            // - Data Part
            zmq::message_t meta_data_part;
            zmq::message_t data_part;
            this->zmq_socket_.recv(&meta_data_part);
            this->zmq_socket_.recv(&data_part);

            // Push the output data frame
            boost::shared_ptr<Frame> data_frame = create_data_frame(meta_data_part, data_part);
            LOG4CXX_TRACE(logger_, "Pushing data frame");
            this->push(data_frame);
        }
        LOG4CXX_INFO(logger_, "Shutting down");
    }

    // Parse the received ZMQ multipart message and extract its contents into a Jungfrau_Message struct
    boost::shared_ptr<Frame> JungfrauProcessPlugin::create_data_frame(zmq::message_t &meta_data_part, zmq::message_t &data_part)
    {
        // Write out the first part of the multipart message (the JSON header) as a string
        std::string json_header(static_cast<char *>(meta_data_part.data()), meta_data_part.size());

        // Create a rapidjson document and parse the header
        rapidjson::Document rapidjson_doc;
        rapidjson_doc.Parse(json_header.c_str());
        if (rapidjson_doc.HasParseError())
        {
            throw std::runtime_error("Failed to parse JSON header");
        }

        // Fetch the meta data from the message
        // and stuff it into a FrameMetaData object
        FrameMetaData frame_meta_data;
        frame_meta_data.set_frame_number(static_cast<uint64_t>(rapidjson_doc["frameIndex"].GetUint64()));
        frame_meta_data.set_dataset_name("data");
        frame_meta_data.set_data_type(raw_16bit);
        frame_meta_data.set_acquisition_ID(std::to_string(rapidjson_doc["acquisition"].GetInt()));

        const rapidjson::Value &shape_value = rapidjson_doc["shape"];
        rapidjson::Value::ConstArray shape_array = shape_value.GetArray();
        dimensions_t dims = {static_cast<dimsize_t>(shape_array[0].GetUint64()), static_cast<dimsize_t>(shape_array[1].GetUint64())};

        frame_meta_data.set_dimensions(dims);
        frame_meta_data.set_compression_type(bslz4);

        // Include the second part of the multipart message (the compressed data)
        size_t compressed_data_size = data_part.size();
        const std::byte *byte_ptr = static_cast<const std::byte *>(data_part.data());

        // Construct a json dict of meta data...
        OdinData::JsonDict json;
        json.add("frame_index", static_cast<uint64_t>(rapidjson_doc["frameIndex"].GetUint64()));
        json.add("row", static_cast<int32_t>(rapidjson_doc["row"].GetInt()));
        json.add("column", static_cast<int32_t>(rapidjson_doc["column"].GetInt()));
        std::vector<uint32_t> shape_vector;
        shape_vector.push_back(static_cast<uint32_t>(shape_array[0].GetUint()));
        shape_vector.push_back(static_cast<uint32_t>(shape_array[1].GetUint()));
        json.add("shape", shape_vector);
        json.add("bit_mode", static_cast<int32_t>(rapidjson_doc["bitmode"].GetInt()));
        json.add("exp_length", static_cast<uint32_t>(rapidjson_doc["expLength"].GetUint()));
        json.add("acquisition", static_cast<uint64_t>(rapidjson_doc["acquisition"].GetUint64()));

        // ...and pass it to the Meta Writer
        this->publish_meta(get_name(), "jungfrau-imagedata", json.str(), json.str());

        // Construct a new data block frame
        boost::shared_ptr<Frame> data_block_frame = boost::shared_ptr<Frame>(new DataBlockFrame(frame_meta_data, byte_ptr, compressed_data_size, 0));

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
