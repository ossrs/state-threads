/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2021 Winlin */

#include <stdio.h>

#include <st.h>

int main(int argc, char** argv)
{
#if defined(__linux__) || defined(__APPLE__)
    st_set_eventsys(ST_EVENTSYS_ALT);
#else
    st_set_eventsys(ST_EVENTSYS_SELECT);
#endif
    st_init();

    for (int i = 0; i < 10000; i++) {
        printf("#%03d, Hello, state-threads world!\n", i);
        st_sleep(1);
    }

    return 0;
}

