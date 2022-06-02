#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include "hit.capnp.h"
#include "hit_file_writer.h"
#include <fcntl.h>
#include <unistd.h>

using namespace std;

// The number of extra columns on each side of the hit to store
const int EXTRA_COLUMNS = 40;

HitFileWriter::HitFileWriter(const string& filename, const FilterbankFile& metadata)
  : metadata(metadata) {
  fd = open(filename.c_str(), O_WRONLY | O_CREAT, 0664);
}

HitFileWriter::~HitFileWriter() {
  close(fd);
}

void HitFileWriter::recordHit(int coarse_channel, int freq_index, int drift_bins,
                              double drift_rate, double snr, const float* input) {
  ::capnp::MallocMessageBuilder message;

  Hit::Builder hit = message.initRoot<Hit>();

  Signal::Builder signal = hit.getSignal();
  int coarse_offset = coarse_channel * metadata.coarse_channel_size;
  int global_index = coarse_offset + freq_index;
  double frequency = metadata.fch1 + global_index * metadata.foff;
  signal.setFrequency(frequency);
  signal.setIndex(freq_index);
  signal.setDriftSteps(drift_bins);
  signal.setDriftRate(drift_rate);
  signal.setSnr(snr);

  // This metadata is just copied over from the input
  Filterbank::Builder filterbank = hit.getFilterbank();
  filterbank.setSourceName(metadata.source_name);
  filterbank.setRa(metadata.src_raj);
  filterbank.setDec(metadata.src_dej);
  filterbank.setTelescopeId(metadata.telescope_id);
  filterbank.setFoff(metadata.foff);
  filterbank.setTsamp(metadata.tsamp);
  filterbank.setTstart(metadata.tstart);
  filterbank.setNumTimesteps(metadata.num_timesteps);
  
  // Extract the subset of columns near the hit
  // final_index is the index of the signal at the last point in time we dedopplered for
  // This could be extra if we padded the signal; if that looks weird then fix it
  int final_index = freq_index + drift_bins;
  int leftmost_index, rightmost_index;
  if (final_index < freq_index) {
    // The hit is drifting left
    leftmost_index = final_index;
    rightmost_index = freq_index;
  } else {
    // The hit is drifting right
    leftmost_index = freq_index;
    rightmost_index = final_index;
  }

  // Find the interval [begin_index, end_index) that we actually want to copy over
  // Pad with extra columns but don't run off the edge
  int begin_index = max(leftmost_index - EXTRA_COLUMNS, 0);
  int end_index = min(rightmost_index + EXTRA_COLUMNS, metadata.coarse_channel_size) - 1;
  int num_channels = end_index - begin_index;
  filterbank.setNumChannels(num_channels);
  filterbank.setFch1(metadata.fch1 + (coarse_offset + begin_index) * metadata.foff);
  filterbank.initData(metadata.num_timesteps * num_channels);
  auto data = filterbank.getData();
  
  // Copy out the subset which would be data[:][begin_index:end_index] in numpy
  for (int tstep = 0; tstep < metadata.num_timesteps; ++tstep) {
    for (int chan = 0; chan < num_channels; ++chan) {
      data.set(tstep * num_channels + chan, input[tstep * metadata.coarse_channel_size + chan]);
    }
  }

  writePackedMessageToFd(fd, message);
}
