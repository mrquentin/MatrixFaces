#pragma once

// Host-only controls for the in-memory blob store, so a test can start from a
// known state. Not part of the blob_store contract: no board implements this.
namespace blob_store_native {

// Forgets every slot, putting them back to "never written".
void reset();

bool wasWritten(const char *name);

}  // namespace blob_store_native
