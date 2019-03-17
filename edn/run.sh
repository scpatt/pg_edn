make install

psql -h localhost -p 5432 -U postgres -f extension.sql

make uninstall
