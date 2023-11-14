#include "spikevm.h"
#include "SglxCppClient.h"
#include "SglxApi.h"
#include <QTimer>
#include <cmath>
#include <iostream>
#include <chrono>
#include <thread>
#include <QDebug>
#include <QTimer>
#include <queue>

SpikeVM::SpikeVM(QObject *parent, const char* myhost, const int port)
    : QObject(parent), myhost(myhost), port(port)
{
    establishConnection();
    initializeFetchContainers();
    updateParameters();
    updateDataBuffers();
    updateBaselineStats_imec();
    updateBaselineStats_ni();
    filterData();
    detectSpikes();
    detectEvents();
    updateEventContents();
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, QOverload<>::of(&SpikeVM::runCycle));

    timer->start(50);
}

bool SpikeVM::establishConnection()
{
    hSglx = sglx_createHandle_std();
    if( sglx_connect( hSglx, myhost, port ) ) {
//        lastMaxReadableScanNum = sglx_getStreamSampleCount(hSglx, 2, 0);
//        bufScanNumEnd = lastMaxReadableScanNum;
        sampleRate_imec = sglx_getStreamSampleRate(hSglx, 2, 0);
        sampleRate_ni = sglx_getStreamSampleRate(hSglx, 0, 0);
        //        ui->map_display->xAxis->setRange(0, (2 * sampleRate / refreshRate));
        return true;
    }
    else {
        printf( "couldn't connect" );
        return false;
    }
}

void SpikeVM::initializeFetchContainers()
{
    cppClient_sglx_get_ints chanCount_container;

    //get number of imec probes
    sglx_getStreamNP(num_probes, hSglx, 2);
    qDebug() << "Initializing fetch containers";
    //for each probe, initialize a fetch container and a data buffer
    for(int probe_ind = 0; probe_ind < num_probes; ++probe_ind){
        imec_fetch_containers.push_back(std::make_shared<cppClient_sglx_fetch>());
        imec_data_buffers.push_back(std::make_shared<std::vector<short>>());
        lastMaxReadableScanNum_imec.push_back(sglx_getStreamSampleCount(hSglx, 2, probe_ind));
        maxReadableScanNum_imec.push_back(lastMaxReadableScanNum_imec[probe_ind]);
        bufScanNumEnd_imec.push_back(0);
        scansToRead_imec.push_back(0);
        availableScansToRead_imec.push_back(0);
        spike_scan_nums.push_back(std::vector<std::vector<t_ull>>());
        spike_times_ms.push_back(std::vector<std::vector<t_ull>>());
        spike_channels.push_back(std::vector<int>());
        earliest_spike_ind_relevant_to_last_search.push_back(std::vector<t_ull>());
        baseline_mean_by_channel_imec.push_back(std::vector<double>());
        baseline_rms_by_channel_imec.push_back(std::vector<double>());

        //get number of channels of each type for this probe
        sglx_getStreamAcqChans(chanCount_container, hSglx, 2, probe_ind);
        std::vector<int> chanCounts = chanCount_container.vint;

        //set the channel and index fields of the fetch container
        imec_fetch_containers[probe_ind]->js = 2;
        imec_fetch_containers[probe_ind]->ip = probe_ind;
        imec_fetch_containers[probe_ind]->n_cs = chanCounts[0];
        imec_fetch_containers[probe_ind]->downsample = dsRatio;
        for(int ch = 0; ch < chanCounts[0]; ++ch) {
            spike_scan_nums[probe_ind].push_back(std::vector<t_ull>());
            spike_times_ms[probe_ind].push_back(std::vector<t_ull>());
            imec_fetch_containers[probe_ind]->chans.push_back(ch);
            earliest_spike_ind_relevant_to_last_search[probe_ind].push_back(0);
            baseline_mean_by_channel_imec[probe_ind].push_back(0);
            baseline_rms_by_channel_imec[probe_ind].push_back(0);
        }
        //copy the contents of this channels vector into a const int array to set as the channel_subset field of the fetch container
        //(not sure why the cpp_client version has the extra vector.)
        imec_fetch_containers[probe_ind]->channel_subset = &imec_fetch_containers[probe_ind]->chans[0];

        //read the channel layout for this probe
        channel_maps.push_back(readGeomMap(probe_ind));

        // initialize waveform vectors
        waveform_x.push_back(QVector<QVector<double>>());
        waveform_y.push_back(QVector<QVector<double>>());
        QVector<double> vector(32, 0);
        for (int ch = 0; ch < chanCounts[0]; ch++) {
            waveform_x[probe_ind].push_back(vector);
            waveform_y[probe_ind].push_back(vector);
        }

        //initialize a filter for this probe
        Biquad* this_probe_biquad = new Biquad( bq_type_highpass, 300/sampleRate_imec, 0, 0);
        imec_filters.push_back(this_probe_biquad);
    }

    qDebug() << "Imec containers intialized";

    //for ni, initialize a fetch container and a data buffer
    sglx_getStreamAcqChans(chanCount_container, hSglx, 0, 0);
    ni_chan_counts = chanCount_container.vint;

    lastMaxReadableScanNum_ni = sglx_getStreamSampleCount(hSglx, 0, 0);
    maxReadableScanNum_ni = lastMaxReadableScanNum_ni;
    bufScanNumEnd_ni = 0;
    scansToRead_ni = 0;
    availableScansToRead_ni = 0;

    ni_fetch_container.js = 0;
    ni_fetch_container.ip = 0;
    ni_fetch_container.n_cs = std::accumulate(ni_chan_counts.begin(), ni_chan_counts.end(), 0);
//    ni_fetch_container.n_cs = chanCounts[3];
    ni_fetch_container.downsample = dsRatio;

//  TODO: decide: do we want to store all conditional relationships between RMS-crossings as distinct events, or just RMS-crossings themselves?
    //For now, just do RMS-crossings themselves.
    //This may actually prove quite important for the event detection algorithm: if we detect an RMS event before it's contextually relevant,
    //and its post-event interval is too long, we may miss the actually relevant occurrence event.

    for(int ch = 0; ch < ni_fetch_container.n_cs; ++ch) {
        ni_fetch_container.chans.push_back(ch);
        event_scan_nums.push_back(std::vector<t_ull>());
        event_times_by_type_ms.push_back(std::vector<t_ull>());
        event_minimum_separation_ms.push_back(500);
        event_before_after_durations_ms.push_back(std::vector<double>());
        event_before_after_durations_ms[ch].push_back(2000);
        event_before_after_durations_ms[ch].push_back(2000);
        earliest_event_ind_relevant_to_last_search.push_back(0);
        baseline_rms_by_channel_ni.push_back(0);
        baseline_mean_by_channel_ni.push_back(0);
        running_sum_squares_by_channel_ni.push_back(0);
        updating_baseline_stats_ni.push_back(false);
        sum_squares_start_scan_num_ni.push_back(0);
    }

    //TODO: add more flexibility for how extra events are incorporated.
    num_event_types = ni_fetch_container.n_cs + 15; //we add an additional 15 event types in order to treat the 16 individual bits of the digital line separately.
    for(int event_type = ni_fetch_container.n_cs; event_type < num_event_types; ++event_type) {
        event_scan_nums.push_back(std::vector<t_ull>());
        event_times_by_type_ms.push_back(std::vector<t_ull>());
        event_minimum_separation_ms.push_back(500);
        event_before_after_durations_ms.push_back(std::vector<double>());
        event_before_after_durations_ms[event_type].push_back(2000);
        event_before_after_durations_ms[event_type].push_back(2000);
        earliest_event_ind_relevant_to_last_search.push_back(0);
    }

    //Initialize spikes_by_event vector
    for(int probe_ind = 0; probe_ind < num_probes; probe_ind++){
        spikes_by_event.push_back(std::vector<std::vector<std::vector<std::vector<double>>>>());
        for(int ch = 0; ch < imec_fetch_containers[probe_ind]->n_cs; ch++){
            spikes_by_event[probe_ind].push_back(std::vector<std::vector<std::vector<double>>>());
            for(int evnt_type_ind = 0; evnt_type_ind < num_event_types; evnt_type_ind++){
                //This is pre-populated such that for every channel on every probe there is an empty vector for each event type.
                //Each time an event of this type is detected, we will push a (possibly empty!) vector of spike times to this vector.
                spikes_by_event[probe_ind][ch].push_back(std::vector<std::vector<double>>());
            }
        }
    }

    ni_fetch_container.channel_subset = &ni_fetch_container.chans[0];
    qDebug() << "Fetch containers intialized";

}

std::vector<int> SpikeVM::readGeomMap(int probe_ind)
{
    struct ChannelInfo {
        int chNumber;
        int s;
        int x;
        int z;
        int u;
    };

    //Get the geomMap for this probe
    cppClient_sglx_get_strs geomMap_container;
    sglx_getGeomMap(geomMap_container, hSglx, probe_ind);
    std::vector<std::string> geomMap = geomMap_container.vstr;

    //initialize the channel map for this probe, and populate it with the geomMap
    //the channel map contains the indices of channels (that is, the indices specifying their acquisition order), sorted by their physical location, with the deepest channel first.
    std::vector<ChannelInfo> channels;

    for(int line_ind = 0; line_ind < geomMap.size(); ++line_ind){
        if(geomMap[line_ind].substr(0, 2) == "ch") {
            ChannelInfo chInfo;
            chInfo.chNumber = std::stoi(geomMap[line_ind].substr(2, geomMap[line_ind].find("_") - 2));
            chInfo.s = std::stoi(geomMap[line_ind].substr(geomMap[line_ind].find("=") + 1));
            line_ind += 1; //move to x value
            chInfo.x = std::stoi(geomMap[line_ind].substr(geomMap[line_ind].find("=") + 1));
            line_ind += 1; //move to z value
            chInfo.z = std::stoi(geomMap[line_ind].substr(geomMap[line_ind].find("=") + 1));
            line_ind += 1; //move to u value
            chInfo.u = std::stoi(geomMap[line_ind].substr(geomMap[line_ind].find("=") + 1));
            channels.push_back(chInfo);

//            // Debug: Print channel info
//            std::cout << "Channel: " << chInfo.chNumber << ", x: " << chInfo.x << ", z: " << chInfo.z << std::endl;

        }
    }

    // Sort based on z-values primarily and x-values secondarily
    std::sort(channels.begin(), channels.end(), [](const ChannelInfo& a, const ChannelInfo& b) {
        if (a.z == b.z) return a.x < b.x;
        return a.z < b.z;
    });

    // Extract sorted channel numbers
    std::vector<int> sortedChannels;
    for (const auto& ch : channels) {
        sortedChannels.push_back(ch.chNumber);
    }

    return sortedChannels;
}

void SpikeVM::updateParameters()
{
    //To prepare this for multithreading, changes from the GUI should be stored in these "queued" variables, and then applied here.
    //This way we can avoid changing the detection parameters in the middle of a cycle.
    absolute_threshold_imec = queued_absolute_threshold_imec;
    rms_threshold_imec = queued_rms_threshold_imec;
    RMS_based_spike_detection = queued_RMS_based_spike_detection;
}

void SpikeVM::updateDataBuffers()
{
//    qDebug() << "Updating data buffers";
    auto start = std::chrono::high_resolution_clock::now();
    t_ull meanScansPerTimerTick = std::round(sampleRate_imec / refreshRate);
    for(int probe_ind = 0; probe_ind < num_probes; probe_ind++){

        lastMaxReadableScanNum_imec[probe_ind] = maxReadableScanNum_imec[probe_ind];
        maxReadableScanNum_imec[probe_ind] = sglx_getStreamSampleCount(hSglx, 2, probe_ind);
        availableScansToRead_imec[probe_ind] = maxReadableScanNum_imec[probe_ind] - lastMaxReadableScanNum_imec[probe_ind];


        scansToRead_imec[probe_ind] = (t_ull) std::min(availableScansToRead_imec[probe_ind], 5 * meanScansPerTimerTick);
        if(scansToRead_imec[probe_ind] == 0){
            continue;
        }

        imec_fetch_containers[probe_ind]->max_samps = scansToRead_imec[probe_ind];

        //    std::vector<short> data(chanCounts[0] * scansToRead);
        t_ull hC = sglx_fetch(*imec_fetch_containers[probe_ind], hSglx, lastMaxReadableScanNum_imec[probe_ind]);
//        t_ull hC = sglx_fetchLatest(*imec_fetch_containers[probe_ind], hSglx);

        if(hC < 1){
            //Should also check here if hC = lastMaxReadableScanNum_imec[probe_ind] + 1
            //Just fetch the latest data
            qDebug() << "gap occured";
            t_ull hC = sglx_fetchLatest(*imec_fetch_containers[probe_ind], hSglx);

            //This means there was a gap since the last fetch, so we should reset the filters.
            resetFilters = true;
        }
        //TODO: lock this data buffer for wrting.

        //Copy imec_fetch_containers[i]->data to the corresponding buffer
        imec_data_buffers[probe_ind]->clear();
        imec_data_buffers[probe_ind]->reserve(imec_fetch_containers[probe_ind]->data.size());
        std::copy(imec_fetch_containers[probe_ind]->data.begin(), imec_fetch_containers[probe_ind]->data.end(), std::back_inserter(*imec_data_buffers[probe_ind]));
        //TODO: unlock this data buffer.




        bufScanNumEnd_imec[probe_ind] = maxReadableScanNum_imec[probe_ind] + scansToRead_imec[probe_ind];
    }



    //Fetch NI data

    //Record end of last ni fetch using last fetch's max readable scan number
    lastMaxReadableScanNum_ni = maxReadableScanNum_ni;

    //Update max readable scan number for this fetch
    maxReadableScanNum_ni = sglx_getStreamSampleCount(hSglx, 0, 0);
    availableScansToRead_ni = maxReadableScanNum_ni - lastMaxReadableScanNum_ni;
    scansToRead_ni = (t_ull) std::min(availableScansToRead_ni, 5 * meanScansPerTimerTick);
    if(scansToRead_ni == 0){
        return;
    }
    ni_fetch_container.max_samps = scansToRead_ni;
    t_ull hC = sglx_fetch(ni_fetch_container, hSglx, lastMaxReadableScanNum_ni);
    if(hC < 1){
        qDebug() << "failed to read \n";
        //Just fetch the latest data
        t_ull hC = sglx_fetchLatest(ni_fetch_container, hSglx);

    }
//    qDebug() << "read " << scansToRead_ni << "\n";


    //Copy NI data to the data buffer
    //TODO: lock this data buffer for wrting.
    ni_data_buffer.clear();
    ni_data_buffer.reserve(ni_fetch_container.data.size());
    std::copy(ni_fetch_container.data.begin(), ni_fetch_container.data.end(), std::back_inserter(ni_data_buffer));
    bufScanNumEnd_ni = lastMaxReadableScanNum_ni + scansToRead_ni;
    //TODO: unlock this data buffer.
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
//    qDebug() << "Time to get data: " << diff.count() << "s\n";
//    qDebug() << "Finished updating buffers";

}


void SpikeVM::filterData()
{
    //Apply each probe's high-pass filter to its data buffer
    for(int probe_ind = 0; probe_ind < num_probes; probe_ind++){
        imec_filters[probe_ind]->applyBlockwiseMem(
                    &(*imec_data_buffers[probe_ind])[0],
                    32767,
                    scansToRead_imec[probe_ind],
                    imec_fetch_containers[probe_ind]->n_cs,
                    0,
                    imec_fetch_containers[probe_ind]->n_cs);
        //Check if there was a gap since the last fetch. If so, reset the filters.
        if(resetFilters){
            zeroFilterTransient(&(*imec_data_buffers[probe_ind])[0], scansToRead_imec[probe_ind], imec_fetch_containers[probe_ind]->n_cs);
        }
    }

    resetFilters = false;

}

void SpikeVM::zeroFilterTransient( short *data, int ntpts, int nchans )
{
    // overwrite with zeros

    if( ntpts > BIQUAD_TRANS_WIDE )
        ntpts = BIQUAD_TRANS_WIDE;

    // zero the first ntpts of each channel
    memset( data, 0, ntpts*nchans*sizeof(qint16) );
}


void SpikeVM::detectSpikes()
{
    //    now loop through each channel and add spikes to the map
    auto start = std::chrono::high_resolution_clock::now();
    QVector<double> vector(32, 0);

    for (int probe_ind = 0; probe_ind < num_probes; probe_ind++)
    {
        int num_chans = imec_fetch_containers[probe_ind]->n_cs;

        if(RMS_based_spike_detection){
            //TODO: lock this probe's data buffer for reading.
            for (int ch = 0; ch < num_chans; ch++) {
                waveform_x[probe_ind].push_back(vector);
                waveform_y[probe_ind].push_back(vector);
                //        qDebug() << ch_threshold_high;
                //        qDebug() << ch_threshold_low;
                for (t_ull i = 0; i < scansToRead_imec[probe_ind]; ) {
                    double val = (double) imec_data_buffers[probe_ind]->at((num_chans * i) + ch );
                    val = (val - baseline_mean_by_channel_imec[probe_ind][ch]) / baseline_rms_by_channel_imec[probe_ind][ch];
                    if ((std::abs(val) > rms_threshold_imec)) {
                        //                    get previous waveforms for this channel:
                        QVector<double> currchan_waveform_x = waveform_x[probe_ind][ch];
                        QVector<double> currchan_waveform_y = waveform_y[probe_ind][ch];
                        currchan_waveform_x.clear();
                        currchan_waveform_y.clear();
                        //                    add this new waveform:
                        int waveform_halfwidth = std::round(sampleRate_imec * .0015 / dsRatio);
                        int jlower = -std::min((int) i, waveform_halfwidth);
                        int jupper = std::min((int) scansToRead_imec[probe_ind] - (int) i , waveform_halfwidth);

                        for(int j = jlower; j < jupper; j++){
                            currchan_waveform_x.push_back((double) j);
                            //                        currchan_waveform_y.push_back(((mult * (double) data[ (chanCounts[0] * (i + j)) + ch ]) - mean));
                            currchan_waveform_y.push_back(((double) imec_data_buffers[probe_ind]->at((num_chans * (i + j)) + ch )) - (baseline_mean_by_channel_imec[probe_ind][ch]));

                        }

                        //record spike index and channel
                        spike_x.push_back((double) ((i * dsRatio)));
                        spike_y.push_back(ch);

                        //TODO: is this the optimal way to store this info? Memory-wise, yes, but in terms of speed of access?
                        spike_scan_nums[probe_ind][ch].push_back(i * dsRatio + lastMaxReadableScanNum_imec[probe_ind] + 1);
                        spike_times_ms[probe_ind][ch].push_back((i * dsRatio + lastMaxReadableScanNum_imec[probe_ind] + 1) * 1000 / sampleRate_imec);
                        spike_channels[probe_ind].push_back(ch);

                        //add this back to full waveform vector:
                        waveform_x[probe_ind][ch] = currchan_waveform_x;
                        waveform_y[probe_ind][ch] = currchan_waveform_y;

                        i += std::round(sampleRate_imec * .015 / dsRatio);
                    } else {
                        ++i;
                    }
                }
            }
        }
        else{
            //TODO: lock this probe's data buffer for reading.
            for (int ch = 0; ch < num_chans; ch++) {
                waveform_x[probe_ind].push_back(vector);
                waveform_y[probe_ind].push_back(vector);

                double ch_threshold_high = absolute_threshold_imec + baseline_mean_by_channel_imec[probe_ind][ch];
                double ch_threshold_low = -absolute_threshold_imec + baseline_mean_by_channel_imec[probe_ind][ch];
                for (t_ull i = 0; i < scansToRead_imec[probe_ind]; ) {
                    double val = (double) imec_data_buffers[probe_ind]->at((num_chans * i) + ch );
                    val = val - baseline_mean_by_channel_imec[probe_ind][ch];
                    if ((val > ch_threshold_high) || (val < ch_threshold_low)) {
                        //get previous waveforms for this channel:
                        QVector<double> currchan_waveform_x = waveform_x[probe_ind][ch];
                        QVector<double> currchan_waveform_y = waveform_y[probe_ind][ch];
                        currchan_waveform_x.clear();
                        currchan_waveform_y.clear();

                        //add this new waveform:
                        int waveform_halfwidth = std::round(sampleRate_imec * .0015 / dsRatio);
                        int jlower = -std::min((int) i, waveform_halfwidth);
                        int jupper = std::min((int) scansToRead_imec[probe_ind] - (int) i , waveform_halfwidth);

                        for(int j = jlower; j < jupper; j++){
                            currchan_waveform_x.push_back((double) j);
                            currchan_waveform_y.push_back(((double) imec_data_buffers[probe_ind]->at((num_chans * (i + j)) + ch )) - baseline_mean_by_channel_imec[probe_ind][ch]);

                        }

                        //record spike index and channel
                        spike_x.push_back((double) ((i * dsRatio)));
                        spike_y.push_back(ch);

                        //TODO: is this the optimal way to store this info? Memory-wise, yes, but in terms of speed of access?
                        spike_scan_nums[probe_ind][ch].push_back(i * dsRatio + lastMaxReadableScanNum_imec[probe_ind] + 1);
                        spike_times_ms[probe_ind][ch].push_back((i * dsRatio + lastMaxReadableScanNum_imec[probe_ind] + 1) * 1000 / sampleRate_imec);
                        spike_channels[probe_ind].push_back(ch);

                        //add this back to full waveform vector:
                        waveform_x[probe_ind][ch] = currchan_waveform_x;
                        waveform_y[probe_ind][ch] = currchan_waveform_y;

                        i += std::round(sampleRate_imec * .015 / dsRatio);
                    } else {
                        ++i;
                    }
                }
            }
        }
        //TODO: unlock this probe's data buffer.
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
//    qDebug() << "Time to get spikes: " << diff.count() << "s\n";
}

void SpikeVM::detectEvents()
{
    //Get digital input channel range for NI
    cppClient_sglx_get_ints chanCount_container;
    sglx_getStreamAcqChans(chanCount_container, hSglx, 0, 0);
    std::vector<int> chanCounts = chanCount_container.vint;
//    int num_chans = chanCounts[3];
    int num_chans = ni_fetch_container.n_cs;


    //TODO: lock this data buffer for reading.
    //An example of more complicated triggering logic on the analog NI channels. For a simple example of TTL-based triggering, scroll down to the following section.
    //here 3 is the pulse channel and 1 is the mic channel, but these can be changed:
    int mic_channel_ind = 1;
    int pulse_channel_ind = 3;
    t_ull rms_refresh_window_ms = 100;
    t_ull pulse_duration_ms = 50;
    //we assume below that rms_refresh_window_ms > pulse_duration_ms, since we use pulse_duration as cover to get a clean rms on the inactive channel.
    int secondary_event_window_seconds = 2;
    double secondary_event_rms_multiplier = 3.2;


    //Handling of the analog NI channels can be in 1 of 5 phases:

    //Phase 0 (default):
    //Look for threshold crossings (2x the channel 3 RMS) on analog channel 3, starting after the appropriate interval since the last such crossing.
    //When found, start updating the sum of squares for channel 1 and record the scan number of the crossing.

    //Phase 1 (after initial threshold crossing found):
    //Check for continued threshold crossings on channel 3 for up to .05s after the initial crossing (in which case proceed to phase 2) or until the crossing stops prematurely (return to phase 0).
    //Continue updating the sum of squares for channel 1.

    //Phase 2: (starts after .05s of sustained crossing):
    //Record an event of type 3 at the time of the initial crossing.
    //Continue updating the sum of squares for channel 1 for another .05s, then move to phase 3.

    //Phase 3: (starts .1s after initial crossing, continues for .1s):
    //Use the just-finished .1s sum of squares for channel 1 to update the RMS values on channel 1.
    //Look out for a crossing on channel 1 of 5x the channel-1 RMS we just computed.
    //If found, record an event of type 1 at the time of the crossing, and stop looking.
    //Use the duration of the .1s to update the sum of squares and RMS for channel 3.
    //If an event was found on channel 1, return to phase 0. Otherwise, move to phase 4.

    //Phase 4: (starts .2s after initial crossing continues for 1.9s):
    //Look out for a crossing on channel 1 of 5x the channel-1 RMS we just computed.
    //If found, record an event of type 1 at the time of the crossing, and return to phase 0.
    //If no event is found, return to phase 0 after 1.9s.


    //Begin reading through the data buffer:
    for(t_ull i = 0; i < scansToRead_ni; i++){
        switch (trigger_phase_ni) {
            case 0:
                //Just look for the start of an crossing on channel 3:
                if(ni_data_buffer[(num_chans * i) + pulse_channel_ind ] > 2 * baseline_rms_by_channel_ni[pulse_channel_ind]){
                    //if found, start updating the sum of squares for channel 1 and record the scan number of the crossing.
                    trigger_phase_ni = 1;
                    threshold_crossing_ongoing_ni = true;
                    threshold_crossing_start_scan_num_ni = i + lastMaxReadableScanNum_ni;
                    sum_squares_start_scan_num_ni[mic_channel_ind] = ni_data_buffer[(num_chans * i) + mic_channel_ind ] * ni_data_buffer[(num_chans * i) + mic_channel_ind ];
                }
                break;

            case 1:
                //check if the channel 3 value has fallen below the threshold:
                if(ni_data_buffer[(num_chans * i) + pulse_channel_ind ] < 2 * baseline_rms_by_channel_ni[pulse_channel_ind]){
                    //reset the ongoing event and sum of squares
                    trigger_phase_ni = 0;
                    threshold_crossing_ongoing_ni = false;
                    updating_baseline_stats_ni[mic_channel_ind] = false;
                    running_sum_squares_by_channel_ni[mic_channel_ind] = 0;
                    break;
                }

                //otherwise, augment the running sum of squares for channel 1:
                running_sum_squares_by_channel_ni[mic_channel_ind] += ni_data_buffer[(num_chans * i) + mic_channel_ind ] * ni_data_buffer[(num_chans * i) + mic_channel_ind ];

                //check if the minimum threshold crossing duration has been attained:
                if((i + lastMaxReadableScanNum_ni - threshold_crossing_start_scan_num_ni) > (sampleRate_ni * pulse_duration_ms / 1000)){
                    //if so, record the event:
                    event_scan_nums[pulse_channel_ind].push_back(i + lastMaxReadableScanNum_ni);
                    event_times_by_type_ms[pulse_channel_ind].push_back((i + lastMaxReadableScanNum_ni) * 1000 / sampleRate_ni);
                    //initialize the spike times vector for this event for all probes and channels:
                    for(int probe_ind = 0; probe_ind < num_probes; probe_ind++){
                        for(int ch = 0; ch < imec_fetch_containers[probe_ind]->n_cs; ch++){
                            spikes_by_event[probe_ind][ch][pulse_channel_ind].push_back(std::vector<double>());
                        }
                    }
                    qDebug() << "found initial event";

                    //and move to phase 2:
                    trigger_phase_ni = 2;
                }
                break;

            case 2:
                //just continue updating the sum of squares for channel 1:
                running_sum_squares_by_channel_ni[mic_channel_ind] += ni_data_buffer[(num_chans * i) + mic_channel_ind ] * ni_data_buffer[(num_chans * i) + mic_channel_ind ];

                //see if a total of .1s has elapsed since the initial crossing:
                if((i + lastMaxReadableScanNum_ni - threshold_crossing_start_scan_num_ni) > (sampleRate_ni * rms_refresh_window_ms / 1000)){
                    //if so, update the RMS for channel 1:
                    baseline_rms_by_channel_ni[mic_channel_ind] = std::sqrt(running_sum_squares_by_channel_ni[mic_channel_ind] / (sampleRate_ni * rms_refresh_window_ms / 1000));

                    //reset the sum of squares for channel 1:
                    running_sum_squares_by_channel_ni[mic_channel_ind] = 0;

                    //reset secondary event tracker
                    found_secondary_event_ni = false;

                    //and move to phase 3:
                    trigger_phase_ni = 3;
                }
                break;

            case 3:
                //check if .1s (or the RMS window, in general) has elapsed since the start of this phase (i.e., .2s, or 2x the RMS window, from the initial crossing):
                if((i + lastMaxReadableScanNum_ni - threshold_crossing_start_scan_num_ni) > (sampleRate_ni * 2 * rms_refresh_window_ms / 1000)){
                    //if so, update the RMS for channel 3:
                    baseline_rms_by_channel_ni[pulse_channel_ind] = std::sqrt(running_sum_squares_by_channel_ni[pulse_channel_ind] / (sampleRate_ni * .1));

                    //reset the sum of squares for channel 3:
                    running_sum_squares_by_channel_ni[pulse_channel_ind] = 0;

                    //and return to phase 0:
                    trigger_phase_ni = 0;
                    break;
                }

                //update the running sum of squares for channel 3:
                running_sum_squares_by_channel_ni[pulse_channel_ind] += ni_data_buffer[(num_chans * i) + pulse_channel_ind ] * ni_data_buffer[(num_chans * i) + pulse_channel_ind ];

                //if we've already found a crossing on channel 1, we're done:
                if(found_secondary_event_ni){
                    break;
                }

                //check for a crossing on channel 1 of [secondary_event_rms_multiplier]x the channel-1 RMS we just computed:
                if(ni_data_buffer[(num_chans * i) + mic_channel_ind ] > secondary_event_rms_multiplier * baseline_rms_by_channel_ni[mic_channel_ind]){
                    //if found, record the event:
                    event_scan_nums[mic_channel_ind].push_back(i + lastMaxReadableScanNum_ni);
                    event_times_by_type_ms[mic_channel_ind].push_back((i + lastMaxReadableScanNum_ni) * 1000 / sampleRate_ni);
                    //initialize the spike times vector for this event for all probes and channels:
                    for(int probe_ind = 0; probe_ind < num_probes; probe_ind++){
                        for(int ch = 0; ch < imec_fetch_containers[probe_ind]->n_cs; ch++){
                            spikes_by_event[probe_ind][ch][mic_channel_ind].push_back(std::vector<double>());
                        }
                    }
                    //indicate that a secondary event was found:
                    found_secondary_event_ni = true;
                    qDebug() << "found secondary event";
                }
                break;

            case 4:
                //Check if 2s has elapsed since the initial crossing:
                if((i + lastMaxReadableScanNum_ni - threshold_crossing_start_scan_num_ni) > (sampleRate_ni * secondary_event_window_seconds)){
                    //if so, return to phase 0:
                    trigger_phase_ni = 0;
                    break;
                }

                //check for a crossing on channel 1 of [secondary_event_rms_multiplier]x the channel-1 RMS we just computed:
                if(ni_data_buffer[(num_chans * i) + mic_channel_ind ] > secondary_event_rms_multiplier * baseline_rms_by_channel_ni[mic_channel_ind]){
                    //if found, record the event:
                    event_scan_nums[mic_channel_ind].push_back(i + lastMaxReadableScanNum_ni);
                    event_times_by_type_ms[mic_channel_ind].push_back((i + lastMaxReadableScanNum_ni) * 1000 / sampleRate_ni);
                    //initialize the spike times vector for this event for all probes and channels:
                    for(int probe_ind = 0; probe_ind < num_probes; probe_ind++){
                        for(int ch = 0; ch < imec_fetch_containers[probe_ind]->n_cs; ch++){
                            spikes_by_event[probe_ind][ch][mic_channel_ind].push_back(std::vector<double>());
                        }
                    }
                    //indicate that a secondary event was found:
                    found_secondary_event_ni = true;
                    qDebug() << "found secondary event";

                    //return to phase 0
                    trigger_phase_ni = 0;
                }
                break;
        }
    }

    //Look for TTL events on the digital input channels.
    //WARNING: indexing of digital lines is VERY subtle! See the spikeglx user manual for details.
    //This is hard-coded to work with 16 bits of digital input data, no more, no less, treating each bit as a binary trigger (so 8 possible events instead of 2^15).
    //This is due purely to our lousy TTL generating protocol at the time of writing.

    for(int ch = std::accumulate(ni_chan_counts.begin(), ni_chan_counts.end(), -chanCounts[3]); ch < std::accumulate(ni_chan_counts.begin(), ni_chan_counts.end(), 0); ch++){
        int event_type_ind = ch;

        for(int bit_ind = 0; bit_ind < 16; bit_ind++){
            event_type_ind = ch + bit_ind;
            t_ull first_relevant_buffer_ind = 0;

            if(!event_scan_nums[event_type_ind].empty()){
                //Get the first scan number in the buffer not in the range of the last detected event of this type:
                t_ull first_relevant_scan_ind = std::max(event_scan_nums[event_type_ind][event_scan_nums[event_type_ind].size() - 1] + (t_ull) std::round(sampleRate_ni * event_minimum_separation_ms[event_type_ind] / 1000), lastMaxReadableScanNum_ni + 1);
                first_relevant_buffer_ind = first_relevant_scan_ind - lastMaxReadableScanNum_ni - 1;
            }

            //On this digital channel, look for events:
            for(t_ull i = first_relevant_buffer_ind; i < scansToRead_ni; ){
                //get the (bit_ind)th bit of the data at this scan number
                bool target_bit = (((ni_data_buffer[(num_chans * i) + ch] & (1 << bit_ind))) != 0);
                if(target_bit){
                    //record event index
                    //TODO: need to reconcile different sampling rates between this and imec. Do it here?
                    event_scan_nums[event_type_ind].push_back(i + lastMaxReadableScanNum_ni + 1);
                    event_times_by_type_ms[event_type_ind].push_back((i + lastMaxReadableScanNum_ni + 1) * 1000 / sampleRate_ni);
                    event_types.push_back(event_type_ind);
                    event_index_within_type.push_back(event_scan_nums[event_type_ind].size() - 1);
                    //initialize the spike times vector for this event for all probes and channels:
                    for(int probe_ind = 0; probe_ind < num_probes; probe_ind++){
                        for(int ch = 0; ch < imec_fetch_containers[probe_ind]->n_cs; ch++){
                            spikes_by_event[probe_ind][ch][event_type_ind].push_back(std::vector<double>());
                        }
                    }

                    //                new_event_pretimes_by_type_ms[event_type_ind].push_back(((i + lastMaxReadableScanNum_ni + 1) * 1000 / sampleRate_ni) - event_before_after_durations_ms[event_type_ind][0]);
                    //                qDebug() << ni_data_buffer[(num_chans * i) + ch];
//                    qDebug() << "Detected event " << event_type_ind << " at t = " << event_times_by_type_ms[event_type_ind][event_times_by_type_ms[event_type_ind].size() - 1];
                    i += std::round(sampleRate_ni * event_minimum_separation_ms[event_type_ind] / 1000);
                }
                else
                {
                    ++i;
                }
            }

        }


    }

    //TODO: can merge and sort events here, rather than in updateEventContents()?

    //TODO: unlock this data buffer.
}


void SpikeVM::updateEventContents()
{
    int lo;
    int hi;
    int mid;
    int foothold;

    //Take the union of the relevant event times, remembering their types:
    std::vector<t_ull> relevant_event_pretimes_ms;
    std::vector<int> relevant_event_types;
    std::vector<int> relevant_event_original_inds;
    for(int event_type_ind = 0; event_type_ind < num_event_types; event_type_ind++){
        if(event_times_by_type_ms[event_type_ind].empty()){
            continue;
        }
        for(int event_ind = earliest_event_ind_relevant_to_last_search[event_type_ind]; event_ind < event_times_by_type_ms[event_type_ind].size(); event_ind++){
            relevant_event_pretimes_ms.push_back(event_times_by_type_ms[event_type_ind][event_ind] - event_before_after_durations_ms[event_type_ind][0]);
            relevant_event_types.push_back(event_type_ind);
            relevant_event_original_inds.push_back(event_ind);
        }
    }

    if(relevant_event_pretimes_ms.empty()){
        return;
    }

    //Sort the event times and types:
    std::vector<int> sorted_event_inds(relevant_event_pretimes_ms.size());
    std::iota(sorted_event_inds.begin(), sorted_event_inds.end(), 0);
    std::sort(sorted_event_inds.begin(), sorted_event_inds.end(), [&relevant_event_pretimes_ms](int i1, int i2){return relevant_event_pretimes_ms[i1] < relevant_event_pretimes_ms[i2];});
    std::vector<t_ull> sorted_event_pretimes_ms(relevant_event_pretimes_ms.size());
    std::vector<int> sorted_event_types(relevant_event_types.size());
    std::vector<int> sorted_event_original_inds(relevant_event_types.size());
    for(int i = 0; i < sorted_event_inds.size(); i++){
        sorted_event_pretimes_ms[i] = relevant_event_pretimes_ms[sorted_event_inds[i]];
        sorted_event_types[i] = relevant_event_types[sorted_event_inds[i]];
        sorted_event_original_inds[i] = relevant_event_original_inds[sorted_event_inds[i]];
    }

    //Iterate over the probes:
    for(int probe_ind = 0; probe_ind < num_probes; probe_ind++){
        bool foundSpikes = false;
        //Iterate over the channels:
        for(int ch = 0; ch < imec_fetch_containers[probe_ind]->n_cs; ch++){
            if(spike_times_ms[probe_ind][ch].empty()){
//                qDebug() << "no spikes on this channel";
                continue;
            }

            //Binary search over the spikes to find the first one that might be relevant:
            //TODO: this earliest relevant spike index might actually come too late if an event with a long pre-duration was detected since the last search!
            lo = earliest_spike_ind_relevant_to_last_search[probe_ind][ch];
            hi = spike_times_ms[probe_ind][ch].size() - 1;
            mid = 0;
            while(lo < hi){
                mid = (lo + hi) / 2;
                if(spike_times_ms[probe_ind][ch][mid] < sorted_event_pretimes_ms[0]){
                    lo = mid + 1;
                }
                else{
                    hi = mid;
                }
            }

            earliest_spike_ind_relevant_to_last_search[probe_ind][ch] = lo;
            foothold = lo;

//            //Check if there are any spikes in the relevant time window:
//            if(spike_times_ms[probe_ind][ch][foothold] > sorted_event_pretimes_ms[sorted_event_pretimes_ms.size() - 1] + event_before_after_durations_ms[sorted_event_types[sorted_event_types.size() - 1]][0] + event_before_after_durations_ms[sorted_event_types[sorted_event_types.size() - 1]][1]){
////                qDebug() << "this channel has no new event-adjacent spikes";
//                continue;
//            }

            //Iterate over the event times:
            for(int event_ind = 0; event_ind < sorted_event_pretimes_ms.size(); event_ind++){

                //Clear the vector of spikes for this event:
                spikes_by_event[probe_ind][ch][sorted_event_types[event_ind]][sorted_event_original_inds[event_ind]].clear();

                //Binary search over the spikes to find the first one that might be relevant:
                lo = foothold;
                hi = spike_times_ms[probe_ind][ch].size() - 1;
                mid = 0;
                while(lo < hi){
                    mid = (lo + hi) / 2;
                    if(spike_times_ms[probe_ind][ch][mid] < sorted_event_pretimes_ms[event_ind]){
                        lo = mid + 1;
                    }
                    else{
                        hi = mid;
                    }
                }

                foothold = lo;


                //Check if this spike is inside the time window for this event:
                if(spike_times_ms[probe_ind][ch][foothold] < sorted_event_pretimes_ms[event_ind] + event_before_after_durations_ms[sorted_event_types[event_ind]][0] + event_before_after_durations_ms[sorted_event_types[event_ind]][1]){
                    //Binary search over the subsequent spikes until we find the first one exceeding this event's time boundary:
                    int lo2 = foothold;
                    int hi2 = spike_scan_nums[probe_ind][ch].size() - 1;
                    int mid2 = 0;
                    while(lo2 < hi2){
                        mid2 = (lo2 + hi2) / 2;
                        if(spike_times_ms[probe_ind][ch][mid2] < sorted_event_pretimes_ms[event_ind] + event_before_after_durations_ms[sorted_event_types[event_ind]][0] + event_before_after_durations_ms[sorted_event_types[event_ind]][1]){
                            lo2 = mid2 + 1;
                        }
                        else{
                            hi2 = mid2;
                        }
                    }
                    //Add all the spikes in this range to the spikes_by_event vector:
                    for(int spike_ind2 = foothold; spike_ind2 < lo2; spike_ind2++){
                        foundSpikes = true;
                        double spike_time_rel_event_pretime = (double) (spike_times_ms[probe_ind][ch][spike_ind2] - sorted_event_pretimes_ms[event_ind]);
                        double spike_time_rel_event_time = spike_time_rel_event_pretime - event_before_after_durations_ms[sorted_event_types[event_ind]][0];
                        spikes_by_event[probe_ind][ch][sorted_event_types[event_ind]][sorted_event_original_inds[event_ind]].push_back(spike_time_rel_event_time);
                    }
//                    //debug: print the number of spikes assigned to this event, for this channel, for this probe:
//                    if(!spikes_by_event[probe_ind][ch][sorted_event_types[event_ind]][sorted_event_original_inds[event_ind]].empty()){
//                        qDebug() << "assigned " << spikes_by_event[probe_ind][ch][sorted_event_types[event_ind]][sorted_event_original_inds[event_ind]].size() << " spikes to ch " << ch;
//                    }
                }
                else{
                    break;
                }
            }
        }
        if(foundSpikes){
//            qDebug() << "found spikes for event";
        }
    }
}

void SpikeVM::updateBaselineStats_imec()
{
    //Read over each data buffer, and update the baseline stats (RMS and mean) for each channel, excluding spikes.
    for(int probe_ind = 0; probe_ind < num_probes; probe_ind++){
        //TODO: lock this probe's data buffer for reading.
        for(int ch = 0; ch < imec_fetch_containers[probe_ind]->n_cs; ch++){
            //TODO: need to exclude spikes in this section

            //Compute the mean of the data in this channel, excluding spikes.
            double sum = 0;
            for(int i = 0; i < scansToRead_imec[probe_ind]; i++){
                sum += imec_data_buffers[probe_ind]->at((imec_fetch_containers[probe_ind]->n_cs * i) + ch);
            }
            baseline_mean_by_channel_imec[probe_ind][ch] = sum / scansToRead_imec[probe_ind];

            //Compute the RMS of the data in this channel, excluding spikes.
            double rms = 0;
            for(int i = 0; i < scansToRead_imec[probe_ind]; i++){
                rms += (imec_data_buffers[probe_ind]->at((imec_fetch_containers[probe_ind]->n_cs * i) + ch) - baseline_mean_by_channel_imec[probe_ind][ch]) * (imec_data_buffers[probe_ind]->at((imec_fetch_containers[probe_ind]->n_cs * i) + ch) - baseline_mean_by_channel_imec[probe_ind][ch]);
            }
            baseline_rms_by_channel_imec[probe_ind][ch] = std::sqrt(rms / scansToRead_imec[probe_ind]);
        }
    }
}

void SpikeVM::updateBaselineStats_ni()
{
    //Read over each data buffer, and update the baseline stats (RMS and mean) for each channel, using all the data in the buffer.
    for(int ch = 0; ch < ni_fetch_container.n_cs; ch++){
        //Loop through the buffer and compute the mean and RMS for this channel:
        double sum = 0;
        double rms = 0;
        for(int i = 0; i < scansToRead_ni; i++){
            sum += ni_data_buffer[(ni_fetch_container.n_cs * i) + ch];
            rms += ni_data_buffer[(ni_fetch_container.n_cs * i) + ch] * ni_data_buffer[(ni_fetch_container.n_cs * i) + ch];
        }
        baseline_mean_by_channel_ni[ch] = sum / scansToRead_ni;
        baseline_rms_by_channel_ni[ch] = std::sqrt(rms / scansToRead_ni);
    }
}

void SpikeVM::runCycle()
{
    updateParameters();
    updateDataBuffers();
    auto start = std::chrono::high_resolution_clock::now();
    filterData();
    detectSpikes();
    detectEvents();
    updateEventContents();
    updateBaselineStats_imec();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
//    qDebug() << "Time to process: " << diff.count() << "s\n";
}

