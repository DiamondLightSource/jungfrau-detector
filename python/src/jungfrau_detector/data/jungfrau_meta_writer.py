"""Implementation of Jungfrau Meta Writer

This module is a subclass of the odin_data MetaWriter and handles Jungfrau specific meta messages, writing them to disk.

James O'Hea, Diamond Light Source
"""

import numpy as np
from odin_data.meta_writer.hdf5dataset import (
    Float32HDF5Dataset,
    Int32HDF5Dataset,
    units,
)
from odin_data.meta_writer.meta_writer import FRAME, MetaWriter
from odin_data.util import construct_version_dict

from jungfrau_detector._version import get_versions

# Morgul data message parameters
FRAME_INDEX = "frameIndex"
ROW = "row"
COLUMN = "column"
SHAPE = "shape"
BIT_MODE = "bitmode"
EXP_LENGTH = "expLength"
ACQUISITION = "acquisition"

# Units
PIXELS = units("pixels")
DEGREES = units("deg")
SECONDS = units("s")
METERS = units("m")
ELECTRON_VOLTS = units("eV")
ANGSTROM = units("A")


def dectris(suffix):
    return "_dectris/{}".format(suffix)


class JungfrauMetaWriter(MetaWriter):
    """Implementation of MetaWriter that also handles Jungfrau meta messages"""

    # Define parameters received on per frame meta message for parent class
    DETECTOR_WRITE_FRAME_PARAMETERS = [
        FRAME_INDEX,
    ]

    def __init__(self, name, directory, endpoints, config):
        # This must be defined for _define_detector_datasets in base class __init__
        self._sensor_shape = config.sensor_shape

        super(JungfrauMetaWriter, self).__init__(name, directory, endpoints, config)
        self._detector_finished = False  # Require base class to check we have finished

        self._series = None

    def _define_detector_datasets(self):
        return [
            Int32HDF5Dataset(FRAME_INDEX),
            Int32HDF5Dataset(ROW),
            Int32HDF5Dataset(COLUMN),
            Int32HDF5Dataset(
                SHAPE,
                shape=tuple(
                    2,
                ),
                rank=1,
            ),
            Int32HDF5Dataset(BIT_MODE),
            Float32HDF5Dataset(EXP_LENGTH),
            Int32HDF5Dataset(ACQUISITION),
        ]

    @property
    def detector_message_handlers(self):
        return {
            "jungfrau-imagedata": self.handle_image_data,
        }

    def handle_image_data(self, header, data):
        """Handle image data message parts 1, 2 and 4"""
        self._logger.debug("%s | Handling image data message", self._name)

        if data[FRAME_INDEX] in self._frame_offset_map:
            self._logger.warning(
                "%s | Base class has already written data for frame %d",
                self._name,
                header[FRAME_INDEX],
            )
            offset = self._frame_offset_map.pop(data[FRAME_INDEX])
            self._add_values(self.DETECTOR_WRITE_FRAME_PARAMETERS, data, offset)

            # If the first frame
            if data[FRAME_INDEX] == 0:
                self._write_dataset(ROW, header[ROW])
                self._write_dataset(COLUMN, header[COLUMN])
                self._write_dataset(SHAPE, header[SHAPE])
                self._write_dataset(BIT_MODE, header[BIT_MODE])
                self._write_dataset(EXP_LENGTH, header[EXP_LENGTH])
                self._write_dataset(ACQUISITION, header[ACQUISITION])
        else:
            # Store this to be written in write_detector_frame_data
            # This will be called when handle_write_frame is called in the
            # base class with this frame number
            self._frame_data_map[data[FRAME_INDEX]] = data

    @staticmethod
    def get_version():
        return ("jungfrau-detector", construct_version_dict(get_versions()["version"]))
