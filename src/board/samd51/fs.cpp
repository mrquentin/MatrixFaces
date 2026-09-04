#include "board/fs.h"

// No filesystem on this board -- see board_caps::kHasFilesystem. The M4
// keeps serving its one embedded page from flash via handleRoot(); nothing
// here is ever called with kHasFilesystem false, but the contract still has
// to link so shared code never #ifdefs around it.
namespace fs {

bool begin() { return false; }

bool serveFile(Client &, const char *) { return false; }

}  // namespace fs
