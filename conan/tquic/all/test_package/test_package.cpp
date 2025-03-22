#include <tquic.h>
#include <cstdio>

int main (int argc, char *argv[])
{
    auto q_config = quic_config_new();
    quic_config_set_initial_max_data                      (q_config, 10000000);
    quic_config_set_initial_max_stream_data_bidi_local    (q_config, 1000000);
    quic_config_set_initial_max_stream_data_bidi_remote   (q_config, 1000000);
    quic_config_set_initial_max_stream_data_uni           (q_config, 1000000);
    quic_config_set_initial_max_streams_bidi              (q_config, 100);
    quic_config_set_initial_max_streams_uni               (q_config, 100);

    auto http3_config = http3_config_new();

    if (http3_config == nullptr) {
        fprintf(stderr, "failed to create HTTP/3 config\n");
        return 1;
    }

    http3_config_free(http3_config);
    quic_config_free(q_config);

    printf ("WELL DONE\n");

    return 0;
}