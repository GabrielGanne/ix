project('ix', 'c',
        version : '0.2.0',
        default_options : [
            'warning_level=3',
            'werror=true',
            'c_std=c99',
        ],
        license : 'MIT',
        meson_version: '>= 0.56',
)


cc = meson.get_compiler('c')
configuration_inc = include_directories('src')

flags = [
    '-Wshadow',
    '-Wstrict-prototypes',
    '-Wmissing-prototypes',
    '-Wno-padded',
]
add_project_arguments(cc.get_supported_arguments(flags), language : 'c')

# write config file
config = configuration_data()
config.set('CONFIG_LOG2_CPU_CACHELINE_SIZE',get_option('lg2-cacheline-size'))
config.set('CONFIG_LOG2_CPU_PAGE_SIZE', get_option('lg2-page-size'))
configure_file(output : 'config.h', configuration : config)
add_project_arguments('-include', 'config.h', language : 'c')

libthread = dependency('threads')

#
# the simple hashtable library (sht)
#

# follow semantic versioning (https://semver.org)
sht_major = '1'  # incompatible API changes
sht_minor = '0'  # add backwards-compatible functionality
sht_patch = '0'  # backwards-compatible bug fixes
sht_version = sht_major + '.' + sht_minor + '.' + sht_patch

sht_sources = files(
        'src/common.h',
        'src/sht.c',
        'src/sht.h',
)
install_headers('src/sht.h')

sht = shared_library('sht',
        sht_sources,
        version : sht_version,
        install : true,
        include_directories : configuration_inc,
        dependencies : [libthread],
)

#
# priority queue (2-ary heap)
#


# follow semantic versioning (https://semver.org)
pq_major = '1'  # incompatible API changes
pq_minor = '0'  # add backwards-compatible functionality
pq_patch = '0'  # backwards-compatible bug fixes
pq_version = pq_major + '.' + pq_minor + '.' + pq_patch

pq_sources = files(
        'src/common.h',
        'src/pqueue.c',
        'src/pqueue.h',
)
install_headers('src/pqueue.h')

pqueue = shared_library('pqueue',
        pq_sources,
        version : pq_version,
        install : true,
        include_directories : configuration_inc,
        dependencies : [libthread],
)

#
# Timer wheel
#

# follow semantic versioning (https://semver.org)
tw_major = '1'  # incompatible API changes
tw_minor = '0'  # add backwards-compatible functionality
tw_patch = '0'  # backwards-compatible bug fixes
tw_version = tw_major + '.' + tw_minor + '.' + tw_patch

tw_sources = files(
        'src/common.h',
        'src/timer-wheel.c',
        'src/timer-wheel.h',
)
install_headers('src/timer-wheel.h')

timerwheel = shared_library('timerwheel',
        tw_sources,
        version : tw_version,
        install : true,
        include_directories : configuration_inc,
        dependencies : [libthread],
)

#
# TESTS
#

all_tests_sources = []
if get_option('tests')
    all_tests_sources += files(
        'test/pqueue-smoketest.c',
        'test/pqueue-unittest.c',
        'test/sht-perf-test.c',
        'test/sht-smoketest.c',
        'test/sht-unittest.c',
        'test/timer-wheel-unittest.c',
    )

    # unit tests
    sht_unittest = executable('sht-unittest',
            files('test/sht-unittest.c'),
            include_directories : include_directories('src', 'test'),
            link_with : sht,
            dependencies : [libthread],
    )
    test('sht-unittest',
        sht_unittest,
        suite : 'unit-tests',
    )

    pq_unittest = executable('pqueue-unittest',
            files('test/pqueue-unittest.c'),
            include_directories : include_directories('src', 'test'),
            link_with : pqueue,
            dependencies : [libthread],
    )
    test('pqueue-unittest',
        pq_unittest,
        suite : 'unit-tests',
    )

    tw_unittest = executable('timer-wheel-unittest',
            files('test/timer-wheel-unittest.c'),
            include_directories : include_directories('src', 'test'),
            link_with : timerwheel,
            dependencies : [libthread],
    )
    test('timer-wheel-unittest',
        tw_unittest,
        suite : 'unit-tests',
    )

    # smoke tests
    sht_smoketest = executable('sht-smoketest',
            files('test/sht-smoketest.c'),
            include_directories : include_directories('src', 'test'),
            link_with : sht,
            dependencies : [libthread],
    )
    test('sht-smoketest',
        sht_smoketest,
        suite : 'smoke-tests',
    )

    pq_smoketest = executable('pqueue-smoketest',
            files('test/pqueue-smoketest.c'),
            include_directories : include_directories('src', 'test'),
            link_with : pqueue,
            dependencies : [libthread],
    )
    test('pqueue-smoketest',
        pq_smoketest,
        suite : 'unit-tests',
    )

    tw_smoketest = executable('timer-wheel-smoketest',
            files('test/timer-wheel-smoketest.c'),
            include_directories : include_directories('src', 'test'),
            link_with : timerwheel,
            dependencies : [libthread],
    )
    test('timer-wheel-smoketest',
        tw_smoketest,
        suite : 'unit-tests',
    )


    # perf test
    executable('sht-perf-test',
        files('test/sht-perf-test.c'),
        include_directories : include_directories('src', 'test'),
            link_with : sht,
            dependencies : [libthread],
    )
endif # tests

#
# DEVTOOLS
#

uncrustify = find_program('uncrustify', required : false)
if uncrustify.found()
    run_target('checkstyle',
        command : [
            uncrustify,
            '-l', 'c',
            '-c', join_paths(meson.project_source_root(), 'devtools', 'uncrustify.cfg'),
            '--check',
            sht_sources,
            pq_sources,
            tw_sources,
            all_tests_sources,
        ],
    )
    run_target('fixstyle',
        command : [
            uncrustify,
            '-l', 'c',
            '-c', join_paths(meson.project_source_root(), 'devtools', 'uncrustify.cfg'),
            '--replace',
            sht_sources,
            pq_sources,
            tw_sources,
            all_tests_sources,
        ],
    )
endif # uncrustify

codespell = find_program('codespell', required : false)
if codespell.found()
    run_target('spelling',
        command : [
            codespell,
            sht_sources,
            pq_sources,
            tw_sources,
            all_tests_sources,
        ]
    )
endif # codespell
