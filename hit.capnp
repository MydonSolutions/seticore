struct Hit {
  # The Signal contains information about the linear signal we found
  # Some of this is redundant, but it's more convenient
  struct Signal {
    # The frequency the hit starts at
    frequency @0 :Float64;
    
    # Which frequency bin the hit starts at
    index @1 :Int32;

    # How many bins the hit drifts over
    driftSteps @2 :Int32;

    # The drift rate in Hz/s
    driftRate @3 :Float64;

    # The signal-to-noise ratio for the hit
    snr @4 :Float32;
  }

  # The Filterbank contains a smaller slice of the larger filterbank that we originally
  # found this hit in.
  struct Filterbank {
    # These fields are like the ones found in FBH5 files.
    sourceName @0 :Text;
    fch1 @1 :Float64;
    foff @2 :Float64;
    tstart @3 :Float64;
    tsamp @4 :Float64;
    ra @5 :Float64;
    dec @6 :Float64;
    telescope @7 :Int32;
    numTimesteps @8 :Int32;
    numChannels @9 :Int32;

    # The length of data should be num_timesteps * num_channels.
    # Storing both of those is slightly redundant but more convenient.
    # The format is a row-major array, indexed by [timestep][channel].
    data @10 :List(Float32);
  }

}