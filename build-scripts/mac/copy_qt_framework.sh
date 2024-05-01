QT_BASE=$1
OUTPUT_BASE=$2

mkdir -p ${OUTPUT_BASE}/Frameworks/QtCore.framework/Versions/A
cp -Rp ${QT_BASE}/lib/QtCore.framework/Versions/A/QtCore ${OUTPUT_BASE}/Frameworks/QtCore.framework/Versions/A
