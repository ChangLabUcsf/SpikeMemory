#ifndef SPIKEVM_H
#define SPIKEVM_H
#include "SglxApi.h"
#include "SglxCppClient.h"
#include "Biquad.h"

#include <QVector>
#include <Qobject>

class SpikeVM : public QObject
{
    Q_OBJECT
public:
    explicit SpikeVM(QObject *parent = nullptr,  const char* myhost = "127.0.0.1", const int port = 4142);

    t_sglxconn S;
    void *hSglx;
    bool running = false;
    std::vector<t_ull> lastMaxReadableScanNum_imec, maxReadableScanNum_imec, bufScanNumEnd_imec, scansToRead_imec, availableScansToRead_imec;
//    t_ull lastMaxReadableScanNum_imec[4] = {0, 0, 0, 0}, maxReadableScanNum_imec[4], bufScanNumEnd_imec[4], scansToRead_imec[4], availableScansToRead_imec[4];
    std::vector<std::vector<std::vector<t_ull>>> spike_scan_nums;
    std::vector<std::vector<std::vector<t_ull>>> spike_times_ms;
    std::vector<std::vector<int>> spike_channels;
    std::vector<std::vector<int>> channel_maps;
    t_ull lastMaxReadableScanNum_ni, maxReadableScanNum_ni, bufScanNumEnd_ni, scansToRead_ni, availableScansToRead_ni;
    t_ull trigger_threshold_crossing_duration_ms;
    std::vector<int> ni_chan_counts;
    std::vector<std::vector<t_ull>> event_scan_nums;
    std::vector<std::vector<t_ull>> event_times_by_type_ms; //event type, event index
    std::vector<t_ull> event_times_ms;
    std::vector<t_ull> event_types;
    std::vector<t_ull> event_index_within_type;
    std::vector<std::vector<std::vector<std::vector<std::vector<double>>>>> spikes_by_event; //probe, channel, event type, event index, timepoint relative to event
    std::vector<std::vector<double>> event_before_after_durations_ms;
    std::vector<double> event_minimum_separation_ms;
    std::vector<std::vector<double>> baseline_mean_by_channel_imec;
    std::vector<std::vector<double>> baseline_rms_by_channel_imec;
    std::vector<double> baseline_mean_by_channel_ni;
    std::vector<double> baseline_rms_by_channel_ni;
    std::vector<double> running_sum_squares_by_channel_ni;
    std::vector<bool> updating_baseline_stats_ni;
    std::vector<t_ull> sum_squares_start_scan_num_ni;
    t_ull threshold_crossing_start_scan_num_ni;
    bool threshold_crossing_ongoing_ni = false;
    bool threshold_crossing_minimum_duration_attained_ni = false;
    bool found_secondary_event_ni = false;
    int trigger_phase_ni = 0;
    std::vector<t_ull> earliest_event_ind_relevant_to_last_search;
    std::vector<std::vector<t_ull>> earliest_spike_ind_relevant_to_last_search; //probe, channel
    double sampleRate_imec;
    double sampleRate_ni;
    double absolute_threshold_imec = 20;
    double queued_absolute_threshold_imec;
    double rms_threshold_imec = 5;
    double queued_rms_threshold_imec;
    double mult; //Conversion factor from int16 to true (pre-gain) V.
    const double refreshRate = 20; //Timer frequency, in Hz.
    const int dsRatio = 1;
//    const char* myhost = "10.37.128.152";
    const char* myhost;
//    const int port = 4142;
    const int port;
    int num_probes;
    int num_event_types;
    bool establishConnection();
    void initializeFetchContainers();
    std::vector<int> readGeomMap(int probe_ind);
    void filterData();
    void zeroFilterTransient( short *data, int ntpts, int nchans );
    void resetFilter();
    void detectSpikes();
    void detectEvents();
    void updateParameters();
    void updateDataBuffers();
    void updateBaselineStats_imec();
    void updateBaselineStats_ni();
    void updateEventContents();
    void runCycle();
    bool resetFilters = false;
    bool RMS_based_spike_detection = false;
    bool queued_RMS_based_spike_detection;
    QVector<double> spike_x, spike_y;
    QVector<QVector<QVector<double>>> waveform_x, waveform_y;

    std::vector<std::shared_ptr<cppClient_sglx_fetch>> imec_fetch_containers;
    cppClient_sglx_fetch ni_fetch_container;
    std::vector<std::shared_ptr<std::vector<short>>> imec_data_buffers;
    std::vector<short> ni_data_buffer;
    std::vector<Biquad*> imec_filters;
};

#endif // SPIKEVM_H
