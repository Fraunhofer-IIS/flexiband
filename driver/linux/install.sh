#!/bin/bash

if [ "$EUID" -ne 0 ]; then
   echo "You have to be root to run this script"
   exit 1;
fi

export CYPRESS_CONFIG=/etc/cyusb.conf
export TELEORBIT_IMG_PATH=/etc/TeleOrbit
export TELEORBIT_BIN_PATH=/usr/local/bin
export TELEORBIT_LIB_PATH=/usr/local/lib

cd src
make clean
make
cd ..

if [ -f ${CYPRESS_CONFIG} ]; then
    # Remove the last line (closing tag </VPD>)
    sed -i '$ d' ${CYPRESS_CONFIG}
   
    # Insert the non-existing TeleOrbit IDs
    if ! grep -w $'27ae\t1013\t\tTeleOrbit GTEC RFFE USB 3.0 Bootloader' ${CYPRESS_CONFIG}; then
        echo $'27ae\t1013\t\tTeleOrbit GTEC RFFE USB 3.0 Bootloader' >> ${CYPRESS_CONFIG}
    fi
    if ! grep -w $'27ae\t1014\t\tTeleOrbit GTEC RFFE USB 3.0' ${CYPRESS_CONFIG}; then
        echo $'27ae\t1014\t\tTeleOrbit GTEC RFFE USB 3.0' >> ${CYPRESS_CONFIG}
    fi
    if ! grep -w $'27ae\t1015\t\tTeleOrbit GTEC RFFE USB 3.0 Bootloader' ${CYPRESS_CONFIG}; then
        echo $'27ae\t1015\t\tTeleOrbit GTEC RFFE USB 3.0 Bootloader' >> ${CYPRESS_CONFIG}
    fi
    if ! grep -w $'27ae\t1016\t\tTeleOrbit GTEC RFFE USB 3.0' ${CYPRESS_CONFIG}; then
        echo $'27ae\t1016\t\tTeleOrbit GTEC RFFE USB 3.0' >> ${CYPRESS_CONFIG}
    fi
    if ! grep -w $'27ae\t1017\t\tTeleOrbit MGSE GTEC USB 3.0 Bootloader' ${CYPRESS_CONFIG}; then
        echo $'27ae\t1017\t\tTeleOrbit MGSE GTEC USB 3.0 Bootloader' >> ${CYPRESS_CONFIG}
    fi
    if ! grep -w $'27ae\t1018\t\tTeleOrbit MGSE GTEC USB 3.0' ${CYPRESS_CONFIG}; then
        echo $'27ae\t1018\t\tTeleOrbit MGSE GTEC USB 3.0' >> ${CYPRESS_CONFIG}
    fi
    if ! grep -w $'27ae\t1025\t\tTeleOrbit GTEC RFFE-2 USB 3.0 Bootloader' ${CYPRESS_CONFIG}; then
        echo $'27ae\t1025\t\tTeleOrbit GTEC RFFE-2 USB 3.0 Bootloader' >> ${CYPRESS_CONFIG}
    fi
    if ! grep -w $'27ae\t1026\t\tTeleOrbit GTEC RFFE-2 USB 3.0' ${CYPRESS_CONFIG}; then
        echo $'27ae\t1026\t\tTeleOrbit GTEC RFFE-2 USB 3.0' >> ${CYPRESS_CONFIG}
    fi
    if ! grep -w $'27ae\t1027\t\tTeleOrbit MGSE-2 GTEC USB 3.0 Bootloader' ${CYPRESS_CONFIG}; then
        echo $'27ae\t1027\t\tTeleOrbit MGSE-2 GTEC USB 3.0 Bootloader' >> ${CYPRESS_CONFIG}
    fi
    if ! grep -w $'27ae\t1028\t\tTeleOrbit MGSE-2 GTEC USB 3.0' ${CYPRESS_CONFIG}; then
        echo $'27ae\t1028\t\tTeleOrbit MGSE-2 GTEC USB 3.0' >> ${CYPRESS_CONFIG}
    fi
    if ! grep -w $'27ae\t10C1\t\tFlexiband USB 3.0 Bootloader' ${CYPRESS_CONFIG}; then
        echo $'27ae\t10C1\t\tFlexiband USB 3.0 Bootloader' >> ${CYPRESS_CONFIG}
    fi
    if ! grep -w $'27ae\t10C2\t\tFlexiband USB 3.0' ${CYPRESS_CONFIG}; then
        echo $'27ae\t10C2\t\tFlexiband USB 3.0' >> ${CYPRESS_CONFIG}
    fi

    # Re-add the closing tag </VPD>
    echo '</VPD>' >> ${CYPRESS_CONFIG}
else
    cp cyusb.conf ${CYPRESS_CONFIG}
fi

mkdir -p ${TELEORBIT_IMG_PATH}
cp flexiband.img ${TELEORBIT_IMG_PATH}/flexiband.img
cp teleorbit.img ${TELEORBIT_IMG_PATH}/teleorbit.img
cp 80-flexiband.rules /etc/udev/rules.d/80-flexiband.rules
cp 80-teleorbit.rules /etc/udev/rules.d/80-teleorbit.rules
cp src/fwload_fx3 ${TELEORBIT_BIN_PATH}/fwload_fx3
cp upload_fx3.sh ${TELEORBIT_BIN_PATH}/upload_fx3.sh
cp src/libcyusb.so ${TELEORBIT_LIB_PATH}/libcyusb.so
cp src/flexiband_fpga ${TELEORBIT_BIN_PATH}/flexiband_fpga
cp flexiband2_rec_I-1m.bit ${TELEORBIT_BIN_PATH}/flexiband2_rec_I-1m.bit
cp flexiband2_setup.sh ${TELEORBIT_BIN_PATH}/flexiband2_setup.sh

chmod +x ${TELEORBIT_BIN_PATH}/fwload_fx3
chmod +x ${TELEORBIT_BIN_PATH}/upload_fx3.sh
chmod +x ${TELEORBIT_BIN_PATH}/flexiband_fpga
chmod +x ${TELEORBIT_BIN_PATH}/flexiband2_setup.sh
