# !/bin/sh 

NUMPARAMS=$#

if [ $NUMPARAMS -lt 4  ]
then 
echo " USAGE ::  "
echo "   sh buildtemplateparallel.sh ImageDimension OutputRoot IterationLimit <images>"
echo  
echo " ImageDimension  -  Dimension of your image, eg 3 for 3D."
echo " OutputRoot      -  Root file name of the output. Should be a file root only, no path information. "
echo " IterationLimit  -  Number of times to build the template. The previous template is used to initialize "
echo "                    each iteration. "
echo " <images>        -  List of images in the current directory, eg *_t1.nii.gz. "
echo  
echo " This script builds a template iteratively from the input images and uses SGE to parallelize the "
echo " registration of every subject to the template. "
echo
echo " We assume all files to be added to the template are in the current directory. You can modify the "
echo " script if you want to relax this assumption, but be sure that the qsubbed jobs get the correct "
echo " absolute path to the images."
echo 
echo " We use the sun grid engine (SGE) to distribute computing via its qsub command. "
echo 
echo " Things within the script that you may need to change for your needs are highlighted by EDIT THIS "
echo
echo " The template will be written to [OutputRoot]template.nii. If the template file exists, it is used as the starting point for "
echo " the new template creation. Otherwise, we create an unbiased starting point by averaging the input dataset. "
exit
fi

#initialization, here, is unbiased 
DIM=$1

# Root of the output name, will produce ${OUTPUTNAME}template.nii
# If this already exists, it will be used to initialize the template building
OUTPUTNAME=$2

# ANTSPATH - you will need to edit this if it is not set before the script is called
# note trailing slash - this is needed
export ANTSPATH=${ANTSPATH:="$HOME/bin/ants/"} # EDIT THIS 

# System specific queue options, eg "-q name" to submit to a specific queue
# It can be set to an empty string if you do not need any special cluster options
QSUBOPTS="" # EDIT THIS


# Mapping Parameters 
  MAXITERATIONS=30x100x20  # EDIT THIS 
  TEMPLATENAME=${OUTPUTNAME}template
  TEMPLATE=${TEMPLATENAME}.nii

  # Gradient step size, smaller in magnitude means more smaller (more cautious) steps
  GRADIENTSTEP="0.25"

ITERATIONLIMIT=$3

shift 3

# Optionally disable qsub for debugging purposes - runs jobs in series
DOQSUB=1


IMAGESETVARIABLE=$*

if [ ! -s $TEMPLATE ] ; then 
echo " No initial template exists. Creating population average image"
 ${ANTSPATH}AverageImages $DIM $TEMPLATE 1  $IMAGESETVARIABLE 
fi

echo  " ANTSPATH  $ANTSPATH "
echo " Mapping Parameters  :: "
echo " MaxIterations :   $MAXITERATIONS " 
echo " OutputName :  $OUTPUTNAME "
echo " template  $TEMPLATE "
echo " Template Update Steps $ITERATIONLIMIT "
echo " Template population :   $IMAGESETVARIABLE "
echo " "
echo " if the files and parameters are all ok then uncomment the exit call below this line  "
echo " ants.sh sets up the per-subject registration parameters -- edit it to change defaults . "
echo " " 
#exit

\rm metriclog.txt ${TEMPLATENAME}warpxlog.txt  ${TEMPLATENAME}warpylog.txt   ${TEMPLATENAME}warpzlog.txt  

# begin loop 
  ITERATLEVEL=(` echo $MAXITERATIONS | tr 'x' ' ' `)
  NUMLEVELS=${#ITERATLEVEL[@]}
i=0

while [  $i -lt ${ITERATIONLIMIT} ]
  do
 # rm -f  ${OUTPUTNAME}*registered*nii
 # rm -f ${OUTPUTNAME}*Warp*nii ${OUTPUTNAME}*warp*nii 
  rm -f  ${OUTPUTNAME}*warp*nii 
  echo " update $i " 
count=100

  if [  $i -eq 0 ] 
  then  
    for (( n = 0 ; n < ${NUMLEVELS}; n++ ))
   do
    val=0;
    if [ $n  -eq  0  ] 
    then 
     val=${ITERATLEVEL}
     ITERATIONS=$val;
    fi
    if [ $n  -gt  0  ] 
    then 
     ITERATIONS=${ITERATIONS}x${val}
    fi
   done
  fi 

  if [  $i -eq 1 ] 
  then  
  for (( n = 0; n<${NUMLEVELS}; n++ )); do  
    val=0;
    if [ $n  -eq  0  ] 
    then 
     val=${ITERATLEVEL}
     ITERATIONS=$val
    fi
    if [ $n  -eq  1  ] 
    then 
     val=${ITERATLEVEL[1]}
     ITERATIONS=${ITERATIONS}x${val}
    fi
    if [ $n  -gt  1  ] 
    then 
     ITERATIONS=${ITERATIONS}x${val}
    fi
   done
  fi 
  if [  $i -gt 1 ] 
  then  
    ITERATIONS=$MAXITERATIONS
  fi 
    echo ITERATIONS $ITERATIONS 
echo $NUMLEVELS 

count=0
ANTSSCRIPTNAME=${ANTSPATH}ants.sh
ANTSAFFSCRIPTNAME=${ANTSPATH}antsaffine.sh
if [ -f $ANTSSCRIPTNAME  ]
    then
    echo " FILE $ANTSSCRIPTNAME OK ! "
       else 
       echo " FILE $ANTSSCRIPTNAME DOES NOT EXIST !!! "
       echo " copy the file to this directory & repeat " 
       echo " Available in ANTS/Scripts directory "
       exit
fi

# Job IDs of jobs submitted to queue in loop below
jobIDs=""

for IMG in $IMAGESETVARIABLE 
do
dir=`pwd`
POO=${OUTPUTNAME}${IMG}
OUTFN=${POO%.*.*}
#echo " ${#OUTFN}  ${#POO} " 
if [ ${#OUTFN} -eq ${#POO} ]
then
OUTFN=${OUTPUTNAME}${IMG%.*}
fi
echo " OUTFN p $OUTFN " 
#if [ $i -gt 0 ] ; then 
exe="${ANTSSCRIPTNAME} $DIM ${dir}/$TEMPLATE  ${dir}/$IMG  ${dir}/$OUTFN $MAXITERATIONS"
#else 
#exe="${ANTSAFFSCRIPTNAME} $DIM ${dir}/$TEMPLATE  ${dir}/$IMG  ${dir}/$OUTFN  "
#fi 
echo " $exe " 

if [ $DOQSUB -gt 0 ]; then
    id=`qsub -S /bin/bash -v ANTSPATH=$ANTSPATH $QSUBOPTS $exe | awk '{print $3}'`
    jobIDs="$jobIDs $id"
    sleep 1
else
    sh $exe
fi

count=`expr $count + 1`;

done

if [ $DOQSUB -gt 0 ]; then 
    echo " submitted $count jobs "

    # now wait for the stuff to finish 
    ${ANTSPATH}waitForSGEQJobs.pl 1 120 $jobIDs
    
    if [ ! $? -eq 0 ]; then
	echo "qsub submission failed - jobs went into error state"
	exit 1;
    fi

fi

echo " finished $i " >> ${TEMPLATENAME}metriclog.txt 

sh ${ANTSPATH}shapeupdatetotemplate.sh $DIM $OUTPUTNAME $GRADIENTSTEP $IMAGESETVARIABLE
# NIREP2  0.5 na*x.nii


if [ $DIM -lt 3  ]
then 
    ${ANTSPATH}ConvertToJpg ${TEMPLATE} ${TEMPLATENAME}$i.jpg
fi
    ${ANTSPATH}MeasureMinMaxMean $DIM ${TEMPLATENAME}warpxvec.nii  ${TEMPLATENAME}warpxlog.txt  1 
    ${ANTSPATH}MeasureMinMaxMean $DIM ${TEMPLATENAME}warpyvec.nii  ${TEMPLATENAME}warpylog.txt  1
if [ $DIM -gt 2  ]
then
    ${ANTSPATH}MeasureMinMaxMean $DIM ${TEMPLATENAME}warpzvec.nii  ${TEMPLATENAME}warpylog.txt  1
fi
fi
i=$((i + 1))

done 


# end loop


cat ${TEMPLATENAME}metriclog.txt 
cat ${TEMPLATENAME}warp*log.txt 


