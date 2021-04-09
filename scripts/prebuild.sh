#! /bin/bash
# this file is call by make, NOT by user

mkdir -p /tmp/rmqtt/

# Git commit ID
commit_id=`git rev-parse --short HEAD`;
# Git branch name
branch_name=$(git symbolic-ref --short -q HEAD)

# src/gen/git_commit_id.h
mkdir -p ../src/gen

f=/tmp/rmqtt/git_commit_id.h
echo "/*!" > $f
echo " * auto generated, DO NOT EDIT!" >> $f
#echo " * date: `env LC_TIME=en_US.UTF-8  date`" >> $f
echo " */" >> $f

echo "#ifndef GIT_COMMIT_ID_H" >> $f
echo "#define GIT_COMMIT_ID_H" >> $f
echo >> $f

echo "#ifdef GIT_COMMIT_ID" >> $f
echo "#undef GIT_COMMIT_ID" >> $f
echo "#endif" >> $f
echo "#define GIT_COMMIT_ID \"${commit_id}\"" >> $f

echo >> $f
echo "#ifdef GIT_BRANCH_NAME" >> $f
echo "#undef GIT_BRANCH_NAME" >> $f
echo "#endif" >> $f
echo "#define GIT_BRANCH_NAME \"${branch_name}\"" >> $f

echo >> $f
echo "#endif /* GIT_COMMIT_ID_H */" >> $f
cp $f ../src/gen/git_commit_id.h;

