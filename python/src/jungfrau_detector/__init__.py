from jungfrau_detector._version import get_versions
from jungfrau_detector.data.jungfrau_meta_writer import JungfrauMetaWriter

__version__ = get_versions()["version"]
del get_versions

__all__ = ["JungfrauMetaWriter", "__version__"]
