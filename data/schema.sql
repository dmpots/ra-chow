--chow run data
--sqlite3 -init schema.sql chow.db 

CREATE TABLE ra_benchmarks(
  id                     INTEGER PRIMARY KEY,
  program                VARCHAR,
  family                 VARCHAR,
  optimization_id        INTEGER, --foreign key
  allocation_detail_id   INTEGER, --foriegn key
  created_on   datetime
);

CREATE TABLE optimizations(
  id                     INTEGER PRIMARY KEY,
  pre_passes             VARCHAR,
  post_passes            VARCHAR
);

CREATE TABLE allocation_details(
  id                             INTEGER PRIMARY KEY,
  --general
  algorithm                      VARCHAR,
  param_string                   VARCHAR,
  num_registers                  INTEGER,
  --chow specific
  bb_size                        INTEGER,
  partitioned                    BOOLEAN,
  load_store_movement            BOOLEAN,
  enhanced_load_store_movement   BOOLEAN
);

CREATE TABLE timings(
  id                             INTEGER PRIMARY KEY,
  function_name                  VARCHAR,
  instruction_count              INTEGER,
  calls                          INTEGER,
  position                       INTEGER,
  ra_benchmark_id                INTEGER --foreign key
);


