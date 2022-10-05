#include <algorithm>
#include <assert.h>
#include <fmt/core.h>
#include "hdf5.h"
#include <iostream>
#include <math.h>
#include "util.h"
#include <vector>

#include "recipe_file.h"

using namespace std;

// The caller is responsible for closing it
hid_t openFile(const string& filename) {
  auto file = H5Fopen(filename.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
  if (file == H5I_INVALID_HID) {
    cerr << "could not open file: " << filename << endl;
    exit(1);
  }
  return file;
}

long evenlyDivide(long a, long b) {
  assert(0 == a % b);
  return a / b;
}

RecipeFile::RecipeFile(const string& filename) :
  file(openFile(filename)),
  ras(getVectorData<double>("/beaminfo/ras", H5T_IEEE_F64LE)),
  decs(getVectorData<double>("/beaminfo/decs", H5T_IEEE_F64LE)),
  obsid(getStringData("/obsinfo/obsid")),
  src_names(getStringVectorData("/beaminfo/src_names")),
  delays(getVectorData<double>("/delayinfo/delays", H5T_IEEE_F64LE)),
  time_array(getVectorData<double>("/delayinfo/time_array", H5T_IEEE_F64LE)),
  npol(getLongScalarData("/diminfo/npol")),
  nbeams(getLongScalarData("/diminfo/nbeams")),
  cal_all(getComplexVectorData("/calinfo/cal_all")),
  nants(evenlyDivide(delays.size(), nbeams * time_array.size())),
  nchans(evenlyDivide(cal_all.size(), nants * npol)) {
  if ((int) ras.size() < nbeams) {
    cerr << fmt::format("could only load {} ras but nbeams = {}\n", ras.size(), nbeams);
    exit(1);
  }
  if ((int) decs.size() < nbeams) {
    cerr << fmt::format("could only load {} decs but nbeams = {}\n", decs.size(), nbeams);
    exit(1);
  }
}

string makeRecipeFilename(const string& directory, const string& obsid) {
  string fixed_obsid = obsid;
  // Someone changed their mind and all the colons should actually
  // be hyphens in obsid's
  replace(fixed_obsid.begin(), fixed_obsid.end(), ':', '-');
  return fmt::format("{}/{}.bfr5", directory, fixed_obsid);
}

RecipeFile::RecipeFile(const string& directory, const string& obsid)
  : RecipeFile(makeRecipeFilename(directory, obsid)) {}

RecipeFile::~RecipeFile() {
  H5Fclose(file);
}

/*
  This is supposed to work for either UTF8 or ASCII.
 */
string RecipeFile::getStringData(const string& name) const {
  auto dataset = H5Dopen(file, name.c_str(), H5P_DEFAULT);
  auto data_type = H5Dget_type(dataset);
  auto native_type = H5Tget_native_type(data_type, H5T_DIR_DEFAULT);
  int size = H5Tget_size(native_type);
  
  vector<char> buffer(size);
  if (H5Dread(dataset, native_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, &buffer[0]) < 0) {
    cerr << "dataset " << name << " could not be read as string\n";
    exit(1);
  }

  H5Tclose(native_type);
  H5Tclose(data_type);
  H5Dclose(dataset);
  
  string answer(buffer.begin(), buffer.end());
  return answer;
}

/*
  Reads a list of strings into the provided output vector.
  We expect them to have variable length.
  The data is converted to the native character type when we read it.
*/
vector<string> RecipeFile::getStringVectorData(const string& name) const {
  auto dataset = H5Dopen(file, name.c_str(), H5P_DEFAULT);
  auto dataspace = H5Dget_space(dataset);
  int npoints = H5Sget_simple_extent_npoints(dataspace);
  auto native_type = H5Tvlen_create(H5T_NATIVE_CHAR);

  // Intermediately store data as the HDF5-provided "variable length sequence" object
  vector<hvl_t> sequences(npoints);

  if (H5Dread(dataset, native_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, &sequences[0]) < 0) {
    cerr << "dataset " << name << " could not be read as string vector\n";
    exit(1);
  }

  vector<string> output;
  for (hvl_t sequence : sequences) {
    char* p = (char*)sequence.p;
    output.push_back(string(p, p + sequence.len));
  }

  H5Tclose(native_type);
  H5Sclose(dataspace);
  H5Dclose(dataset);

  return output;
}

/*
  Reads a variable-length array into a vector.
  You need to provide two types to call this - T is the type that C++ uses, hdf5_type
  is the type that the HDF5 library is using.
  This doesn't work for strings; call getStringVectorData for that.
*/
template <class T>
vector<T> RecipeFile::getVectorData(const string& name, hid_t hdf5_type) const {
  auto dataset = H5Dopen(file, name.c_str(), H5P_DEFAULT);
  auto dataspace = H5Dget_space(dataset);
  int npoints = H5Sget_simple_extent_npoints(dataspace);
  vector<T> output(npoints);
  if (H5Dread(dataset, hdf5_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, &output[0]) < 0) {
    cerr << "dataset " << name << " could not be read with getVectorData\n";
    exit(1);
  }
  
  H5Sclose(dataspace);
  H5Dclose(dataset);

  return output;
}

vector<thrust::complex<float> > RecipeFile::getComplexVectorData(const string& name) const {
  hid_t complex_type = H5Tcreate(H5T_COMPOUND, 8);
  H5Tinsert(complex_type, "r", 0, H5T_IEEE_F32LE);
  H5Tinsert(complex_type, "i", 4, H5T_IEEE_F32LE);

  vector<thrust::complex<float> > output =
    getVectorData<thrust::complex<float> >(name, complex_type);
 
  H5Tclose(complex_type);

  return output;
}

// Reads a single long integer.
long RecipeFile::getLongScalarData(const string& name) const {
  auto dataset = H5Dopen(file, name.c_str(), H5P_DEFAULT);
  auto dataspace = H5Dget_space(dataset);
  int npoints = H5Sget_simple_extent_npoints(dataspace);
  if (npoints != 1) {
    cerr << "expected scalar at " << name << " but got " << npoints << " data values\n";
    exit(1);
  }

  long output;
  if (H5Dread(dataset, H5T_STD_I64LE, H5S_ALL, H5S_ALL, H5P_DEFAULT, &output) < 0) {
    cerr << "dataset " << name << " could not be read with getLongScalarData\n";
    exit(1);
  }
  
  H5Sclose(dataspace);
  H5Dclose(dataset);

  return output;  
}

/*
  Figures out what time array index to use for beamforming, for the given time.
  This is the index for which time_array[index] is closest to the provided time.
 */
int RecipeFile::getTimeArrayIndex(double time) const {  
  // The two candidates are the ones to either side of time
  auto right_iter = lower_bound(time_array.begin(), time_array.end(), time);
  if (time_array.begin() == right_iter) {
    return 0;
  }
  auto left_iter = right_iter - 1;

  // Check which one is closer
  double right_dist = *right_iter - time;
  assert(right_dist >= 0);
  double left_dist = time - *left_iter;
  assert(left_dist >= 0);

  if (right_dist < left_dist) {
    return right_iter - time_array.begin();
  }
  return left_iter - time_array.begin();
}

/*
  Accessor to delays.
  Delays are row-major organized by:
    delays[time_array_index][beam][antenna]
*/
double RecipeFile::getDelay(int time_array_index, int beam, int antenna) const {
  return delays[((time_array_index * nbeams) + beam) * nants + antenna];
}

/*
  Accessor to cal_all.
  cal_all is row-major organized by:
    cal_all[frequency][polarity][antenna]
  TODO: figure out what this should actually be named.
 */
thrust::complex<float> RecipeFile::getCal(int frequency, int polarity, int antenna) const {
  return cal_all[((frequency * npol) + polarity) * nants + antenna];
}

/*
  Generate the beamforming coefficients for the given parameters.

  There are two ways parameters give us different coefficients. They specify the time, and
  the frequency range we are interested in.

  time_array_index corresponds to which entry in time_array to use.

  Some parameters describe the underlying raw file.
  raw_start_channel is what channel in the recipe file the raw file starts at
    (the SCHAN header)
  raw_num_channels is how many (coarse) channels the raw file has has
  raw_center_mhz is the center of the entire raw file.
    (the OBSFREQ header)
  raw_bandwidth_mhz is the width of the entire raw file, negative for reversed.
    (the OBSBW header)

  We only want coefficients for one subband of the raw file, specified by
  subband_start and subband_size. These are indices relative to the raw file.
 */
void RecipeFile::generateCoefficients(int time_array_index,
				      int raw_start_channel,
                                      int raw_num_channels,
                                      float raw_center_mhz,
				      float raw_bandwidth_mhz,
				      int subband_start,
				      int subband_size,
                                      thrust::complex<float>* coefficients) const {
  assert(subband_start + subband_size <= raw_num_channels);
  assert(raw_start_channel + raw_num_channels <= nchans);
  
  float chan_bandwidth_ghz = raw_bandwidth_mhz / raw_num_channels * 0.001;
  float raw_center_index = (raw_num_channels - 1.0) / 2.0;

  int output_index = 0;
  for (int i = 0; i < subband_size; ++i) {
    // Index within the raw file
    int raw_channel_index = subband_start + i;

    // Index within the recipe file
    int recipe_channel_index = raw_start_channel + raw_channel_index;
    assert(recipe_channel_index < nchans);
    
    // Calculate the center of this coarse channel
    float chan_center_ghz = raw_center_mhz * 0.001 +
      (raw_channel_index - raw_center_index) * chan_bandwidth_ghz;

    for (int beam = 0; beam < nbeams; ++beam) {
      for (int polarity = 0; polarity < npol; ++polarity) {
        for (int antenna = 0; antenna < nants; ++antenna) {
          float tau = getDelay(time_array_index, beam, antenna);
          auto cal = getCal(recipe_channel_index, polarity, antenna);

          // Figure out how much to rotate
          float angle = 2 * M_PI * chan_center_ghz * tau;
          float cos_val = cos(angle);
          float sin_val = sin(angle);

          // Rotate to get the coefficients
          float real = cal.real() * cos_val - cal.imag() * sin_val;
          float imag = cal.real() * sin_val + cal.imag() * cos_val;
          coefficients[output_index++] = thrust::complex<float>(real, imag);
        }
      }
    }
  }
}

vector<double> RecipeFile::getRAsInHours() const {
  vector<double> output;
  for (double ra : ras) {
    output.push_back(radiansToHours(ra));
  }
  return output;
}

vector<double> RecipeFile::getDecsInDegrees() const {
  vector<double> output;
  for (double dec : decs) {
    output.push_back(radiansToDegrees(dec));
  }
  return output;
}
