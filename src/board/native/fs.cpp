#include "board/fs.h"

// Host tests never mount a filesystem; see board_caps::kHasFilesystem.
namespace fs {

bool begin() { return false; }

bool serveFile(Client &, const char *) { return false; }

}  // namespace fs
