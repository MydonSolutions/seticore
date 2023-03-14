#include <boost/filesystem.hpp>
#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <errno.h>
#include "event_file_writer.h"
#include <fmt/core.h>
#include "hit.capnp.h"
#include "hit_file_writer.h"
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include "util.h"

using namespace std;


EventFileWriter::EventFileWriter(const string& filename,
                                 const vector<FilterbankMetadata>& metadatas)
  : metadatas(metadatas), tmp_filename(filename + ".tmp"), final_filename(filename)
{
  fd = open(tmp_filename.c_str(), O_WRONLY | O_CREAT, 0664);
  if (fd < 0) {
    int err = errno;
    fatal(fmt::format("could not open {} for writing. errno = {}", tmp_filename, err));
  }
}

EventFileWriter::~EventFileWriter() {
  close(fd);
  boost::filesystem::rename(tmp_filename, final_filename);
}

// hits are null when there is no hit for a region.
// Both of the input vectors should be parallel to the input files.
void EventFileWriter::write(const vector<DedopplerHit*>& hits,
                            const vector<float*> inputs) {
  assert(hits.size() == metadatas.size());
  assert(inputs.size() == metadatas.size());

  ::capnp::MallocMessageBuilder message;
  Event::Builder event = message.initRoot<Event>();
  event.initHits(hits.size());

  assert(hits[0] != NULL);

  /*
  int beam = hits[0]->beam;
  int coarse_channel = hits[0]->coarse_channel;
  */

  for (int i = 0; i < (int) hits.size(); ++i) {
    // TODO
  }
}
