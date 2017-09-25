#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import xml.etree.ElementTree as ET
import xml.dom.minidom as dom
import sys
import argparse
from urllib.request import urlopen

# Namespace of the Iana
ns = {'ns': 'http://www.iana.org/assignments'}

# Download IPFIX Iana file
def dowload_file(url):
    print("Downloading file from '" + url + "'")
    file = urlopen(url)
    return file

# Add element to the 'dst'
# name = name of the new SubElement
# src  = text value of the new element
# dst  = root element, new element will append
# return: parsed element
def add_el(name, src, dst):
    if src is not None:
        el = ET.Element(name)
        el.text = src
        dst.append(el)
        return el

# Create root element
def create_root():
    root   = ET.Element("ipfix-elements")
    scope  = ET.Element("scope")
    add_el("pen",    "1",    scope)
    add_el("name",   "iana", scope)
    add_el("biflow", "29305",   scope).set("mode", "pen")
    root.append(scope)
    return root

# Find text with 'name' in 'rec'
# name = searched text
# rec  = root element from a xml source file
def find_text(name, rec):
    rec = rec.find('ns:' + name, ns)
    if rec is not None:
        return rec.text

units = ["none", "bits", "octets", "packets", "flows", "seconds", "milliseconds",
         "microseconds", "nanoseconds", "4-octet words", "messages", "hops", "entries", "frames"]
# Find if text contain one of the substrings of units
# True if contain, otherwise False
def is_unit(text):
    for unit in units:
        if unit in text:
            return True
    return False

# Find unit in a text
# name = searched text
# rec  = root element from a xml source file
def find_unit(name, rec):
    text = find_text(name, rec)
    if text is not None:
        if is_unit(text):
            return text.split()[-1]

# Find number in element with name 'name' in root element 'rec'
# Number cannot be 0
# name = element's name
# rec = root element
# return: Founded element if is number, toherwise None
def find_numb(name, rec):
    text = find_text(name, rec)
    if text is not None:
        if text.isnumeric():
            return text

# Create one element from a 'rec'
# rec = source with one record
# return: parsed element
# note: element must contain ID, name and unit, if doesn't return None
def parse_rec(rec):
    el = ET.Element("element")
    if add_el("id", find_numb("elementId", rec), el) is None:
        raise Exception("Wrong ID of the record")
    if add_el("name", find_text("name", rec), el) is None:
        raise Exception("Wrong name of the record")
    if add_el("dataType", find_text("dataType", rec), el) is None:
        raise Exception("Wrong data type of the record")
    add_el("dataSemantic", find_text("dataTypeSemantic", rec), el)
    add_el("units",        find_unit("units",            rec), el)
    add_el("status",       find_text("status",           rec), el)
    return el

# Find root element with information elements in a file
# file = file
# return: founded element if exist, otherwise None
def find_root(file):
    for root in ET.parse(file).getroot().findall('ns:registry', ns):
        if root.attrib == {'id': 'ipfix-information-elements'}:
            return root

# Parse all records from a 'src' to the 'dst'
# src = source root element with refined records
# dst = root element where all parsed records will append
def parse_records(src, dst):
    for rec in src.findall('ns:record', ns):
        try:
            tmp = parse_rec(rec)
        except Exception as error:
            print("    Element with ID: " + str(rec[1].text).ljust(11) + " is ignored: "+str(error))
            continue
        dst.append(tmp)

# Parse XML file to XML file in better form
# file = file
def parse_file(file):
    print("Parsing file")
    iana_root = find_root(file)
    if iana_root is None:
        print("Element 'registry' with id='ipfix-information-elements' not found")
        return -1
    out_root = create_root()
    parse_records(iana_root, out_root)
    return out_root

# Write text to the file
# text = string with text to write to file
def write_file(text, file):
    print("Writing to file '" + file + "'")
    with open(file, "w") as file:
        text = text.splitlines()
        file.write(text[0])
        file.write("\n<!--\nThis code was generated by a tool.\n\n\
Changes to this file may cause incorrect behavior\n\
and will be lost when the list is regenerated.\n-->\n")
        for line in text[1:]:
            file.write(line + "\n")
        file.close()

# Get args from arguments line
def get_args():
    url = "https://www.iana.org/assignments/ipfix/ipfix.xml"
    parser = argparse.ArgumentParser(description="Parse Iana elements")
    parser.add_argument("-f", dest="file", default="iana.xml",
    help="Change name of the output file, default is 'iana.xml'")
    parser.add_argument("-l", dest="url", default=url,
    help="Change URL from which will be downloaded iana information elements, default is " + url)
    return parser.parse_args()

# Download file and parse to better form
def main():
    args = get_args()
    try:
        file = dowload_file(args.url)
    except:
        print("Error while downloading file, check internet connection")
        return -1
    try:
        root = parse_file(file)
    except:
        print("Error while parsing file")
        return -1
    try:
        res = dom.parseString(ET.tostring(root)).toprettyxml(indent="    ", encoding ="utf-8")
        write_file(res.decode("utf-8"), args.file)
    except:
        print("Error while saving file")
        return -1

if __name__ == "__main__":
    sys.exit(main())
