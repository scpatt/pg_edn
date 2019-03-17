EXTENSION = edn              # the extension name
DATA      = edn--0.0.1.sql   # script files to install
MODULES   = edn

OBJS = edn.o murmur3.o

PG_CPPFLAGS = -fPIC -Iinclude -I$(libpq_srcdir)

MODULE_big = edn

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
