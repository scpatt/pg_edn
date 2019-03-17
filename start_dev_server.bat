SET pg_build_dir=C:\msys64\mingw64

FOR /D %%i IN (%pg_build_dir%\data*) DO RD /S /Q "%%i"

cd %pg_build_dir%\bin

initdb --pgdata=%pg_build_dir%\data --username=postgres --auth=trust

pg_ctl -D %pg_build_dir%\data start

PAUSE
