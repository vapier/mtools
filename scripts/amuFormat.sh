#!/bin/csh 
#
#  amuFormat.sh  Formats various types and sizes of PC-Cards, according to the
#  AMU-specification
#  
#  parameters:   $1:   Card Type: The Card Type is written as disk/volume-label
#                      to the boot-record
#                      The string should have a length of max. 11 characters.
# 
#                $2:   Drive character (b:, c:)
#
#  10-12-2003    lct   created
#
set vers=1.4
set fdrive="f:"


#echo "debug: $0,$1,$2,$3,$4"

#
# main()
#
echo "amuFormat $vers started..."

if ( $#argv == 0 ) then
   echo "Usage: amuFormat.sh <Card Type> <drive>"
   echo "<Card Type> has to be defined in amuFormat.sh itself"
   echo "<drive> has to be defined in mtools.conf"
   echo ""
   exit 0
endif


echo "Formatting card in slot $2 as $1"

if ( $1 == "8MBCARD-FW" ) then
   
   ## determine formatting drive
   if ( ($2 == "b:") || ($2 == "B:") ) then
      set fdrive = "f:"
   else if ( ($2 == "c:") || ($2 == "C:" ) ) then
           set fdrive = "g:"
   else
      echo "Drive $2 not supported."
      exit 1
   endif
         
   ## initialise partition table
   mpartition -I $fdrive
   
   # write a partition table
   mpartition -c -t245 -h2 -s32 -b32 $fdrive

   ## using the f: or g: drive for fat12 formatting...
   ## see mtools.conf file...
   mformat -c8 -v 8MBCARD-FW $fdrive
 
   minfo $2
   mdir  $2
   
   echo "done."
   exit 0

endif

if ( $1 == "32MBCARD-FW" ) then
   
   #from amu_toolkit_0_6: mformat -t489 -h4 -c4 -n32 -H32 -r32 -vPC-CARD -M512 -N0000 c:
  
   ## initialise partition table
   mpartition -I $2

   ## write a partition table
   mpartition -c -t489 -h4 -s32 -b32 $2

   ## write boot-record, two FATs and a root-directory 
   mformat -c4 -v 32MBCARD-FW $2

   minfo $2
   mdir  $2

   echo "done."
   exit 0

endif

if ( $1 == "64MBCARD-FW" ) then

   echo "***** WARNING: untested on AvHMU, exiting *****"
   exit 0

   ## initialise partition table
   mpartition -I $2

   ## write a partition table
   mpartition -c -t245 -h2 -s32 -b32 $2

   ## write boot-record, two FATs and a root-directory
   mformat -c8 -v 64MBCARD-FW $2

   minfo $2
   mdir  $2

   echo "done."
   exit 0


endif


if ( $1 == "1GBCARD-FW" ) then

   # from amu_toolkit_0_6: mformat -t2327 -h16 -c64 -n63 -H63 -r32 -v AMU-CARD -M512 -N 0000 c:
   
   echo "***** WARNING: untested on AvHMU *****"
   
   ## initialise partition table
   mpartition -I $2

   # write a partition table
   mpartition -c -t2327 -h16 -s32 -b32 $2

   ## write boot-record, two FATs and a root-directory
   mformat -c64 -v 1GBCARD-FW $2

   minfo $2
   mdir  $2

   echo "done."
   exit 0


endif

if ( $1 == "64MBCARDSAN" ) then
   
   # from amu_toolkit_0_6: mformat -t489 -h8 -c4 -n32 -H32 -r32 -v AMU-CARD -M512 -N 0000 c:

   ## initialise partition table
   mpartition -I $2

   # write a partition table
   mpartition -c -t489 -h8 -s32 -b32 $2

   ## write boot-record, two FATs and a root-directory
   mformat -c4 -v 64MBCARDSAN $2

   minfo $2
   mdir  $2

   echo "done."
   exit 0


endif

#
# insert new cards here...
# 

echo "Card not supported."
exit 1
 
 
