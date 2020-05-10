#!/bin/sh
while getopts e:h: OPT; do
    case $OPT in
        "e") exp=$OPTARG;;
        "h") help=1;;
    esac
done
if [ -z "$exp" -a -z "$help" ]; then
    exp=`cat - | tr -d '\n' | sed -e 's/  */ /g'`
fi
if [ `which sbcl` ]; then
    lisp="sbcl --script compile.lisp"
elif [ `which clisp` ]; then
    lisp="clisp compile.lisp"
else
    echo "You need to install SBCL or CLISP"
fi
if [ -z "$exp" ]; then
      cat <<EOF
      Usage:
      ./compile.sh -e "(car '(a b c))" > car.rl
      or
      ./compile.sh < sum100.lisp > sum100.rl
EOF
else
    $lisp | sed -e 's/;.*$//g' | tr -d '\n' | sed -e 's/  */ /g' | \
        awk "{sub(\"write_exp_here\", \"$exp\")}{print}"
fi
