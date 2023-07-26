:: Create FLAC file from an modifyed 40msps CX Card 8-bit .u8 file.
pushd %~dp0
echo Encoding Unsinged 8-bit to FLAC Compressed ... 
flac.exe -f "%~1" --best --sample-rate=40000 --sign=unsigned --channels=1 --endian=little --bps=8 "%~n1.flac"
echo Done. 
PAUSE