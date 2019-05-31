#!/bin/bash
#
# file: tildes.sh
#
# Cambia tildes con escape en archivos latex por unicodes
#
# USO: ./tildes.sh archivo.tex
#
#

sed --in-place= -e "s/\\\'a/á/g" -e "s/\\\'e/é/g" -e "s/\\\'i/í/g" -e "s/\\\'o/ó/g" -e "s/\\\'u/ú/g" -e "s/\\\~n/ñ/g" $1

