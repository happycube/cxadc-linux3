:: Create FLAC file from an stock CX Card 28.6msps 8-bit capture .u8 file.
pushd %~dp0
echo Encoding Unsinged 8-bit to FLAC Compressed ... 
flac.exe -f "%~1" --best --sample-rate=28636 --sign=unsigned --channels=1 --endian=little --bps=8 "%~n1.flac"
echo Done. 
PAUSE