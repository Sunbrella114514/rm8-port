"""Version numbers of the extractor and of the data layout it produces."""

# Version of this extractor (bump on every behaviour change).
EXTRACTOR_VERSION = '1.2.0'

# Version of the extracted data layout (<out>/game + <out>/scenes + manifest.json). The game app checks this number:
# bump it whenever an exporter changes the format of a file the app reads, so that an older extraction is reported as
# "please run the extractor again" instead of crashing.
DATA_FORMAT = 2

MANIFEST_NAME = 'manifest.json'
