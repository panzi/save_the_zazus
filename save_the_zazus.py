#!/usr/bin/env python

from __future__ import print_function, division

import tempfile
import zipfile
import shutil
import os
import sys
import platform

from os.path import join as path_join, relpath
from collections import OrderedDict

SYSTEM = platform.system()
DIR = os.path.dirname(os.path.abspath(__file__))

def load_file(filename):
	with open(path_join(DIR, 'package', filename), 'rb') as fp:
		return fp.read()

def update_zip(archive, filemap):
	tempdir = tempfile.mkdtemp()
	replaced = set()
	try:
		tempname = os.path.join(tempdir, 'tmp.zip')

		with zipfile.ZipFile(archive, 'r') as zipread:
			with zipfile.ZipFile(tempname, 'w') as zipwrite:
				for item in zipread.infolist():
					if item.filename in filemap:
						data = load_file(item.filename)
						replaced.add(item.filename)
						print('updating file:', item.filename)
					else:
						data = zipread.read(item.filename)
					zipwrite.writestr(item, data, item.compress_type)

				for filename in filemap:
					if filename not in replaced:
						print('adding file:', filename)
						data = load_file(filename)
						zipwrite.writestr(item, data, item.compress_type)

		shutil.move(tempname, archive)

	finally:
		shutil.rmtree(tempdir)

def create_filemap(pkgdir):
	filemap = OrderedDict()
	for dirname, dirs, files in os.walk(pkgdir):
		for filename in files:
			path = relpath(path_join(dirname, filename), pkgdir)
			filemap[path] = True
	return filemap

if SYSTEM == 'Linux':
	paths = [
		".local/share/Steam/SteamApps/common/Save the Dodos/package.nw",
		".steam/Steam/SteamApps/common/Save the Dodos/package.nw"
	]

	def find_path_ignore_case(basedir, components):
		path = basedir
		for name in components.split('/'):
			lower_name = name.lower()
			name_map = dict((filename.lower(), filename)
				for filename in os.listdir(path))

			try:
				filename = name_map[lower_name]
			except KeyError:
				return None
			else:
				path = path_join(path, filename)

		return path

	def find_archive():
		home = os.getenv("HOME")

		for path in paths:
			archive = find_path_ignore_case(home, path)
			if archive is not None:
				return archive

		raise FileNotFoundError('package.nw not found.')

elif SYSTEM == 'Windows':
	try:
		from winreg import OpenKeyEx, QueryValueEx, KEY_QUERY_VALUE, \
		                   REG_SZ, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE
	except ImportError:
		from _winreg import OpenKeyEx, QueryValueEx, KEY_QUERY_VALUE, \
		                    REG_SZ, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE

	reg_keys = [
		# Have confirmed sigthing of these keys:
		( HKEY_LOCAL_MACHINE, "Software\\Valve\\Steam",              "InstallPath" ),
		( HKEY_LOCAL_MACHINE, "Software\\Wow6432node\\Valve\\Steam", "InstallPath" ),
		( HKEY_CURRENT_USER,  "Software\\Valve\\Steam",              "SteamPath"   ),

		# All the other possible combination, just to to try everything:
		( HKEY_CURRENT_USER,  "Software\\Wow6432node\\Valve\\Steam", "SteamPath"   ),
		( HKEY_LOCAL_MACHINE, "Software\\Valve\\Steam",              "SteamPath"   ),
		( HKEY_LOCAL_MACHINE, "Software\\Wow6432node\\Valve\\Steam", "SteamPath"   ),
		( HKEY_CURRENT_USER,  "Software\\Valve\\Steam",              "InstallPath" ),
		( HKEY_CURRENT_USER,  "Software\\Wow6432node\\Valve\\Steam", "InstallPath" )
	]

	def get_path_from_registry(hKey, lpSubKey, lpValueName):
		hSubKey = OpenKeyEx(hKey, lpSubKey, 0, KEY_QUERY_VALUE)
		value, reg_type = QueryValueEx(hSubKey, lpValueName)
		if reg_type != REG_SZ:
			raise TypeError('Registry key has wrong type')
		return path_join(value, "steamapps\\common\\Save the Dodos\\package.nw")

	def find_archive():
		for hKey, lpSubKey, lpValueName in reg_keys:
			try:
				path = get_path_from_registry(hKey, lpSubKey, lpValueName)
			except:
				continue
			else:
				return path

		raise FileNotFoundError('package.nw not found.')

elif SYSTEM == 'Darwin':
	raise OSError('Mac OS X not yet supported.')
else:
	raise OSError('System not supported: %s' % SYSTEM)

def main():
	try:
		pkgdir = path_join(DIR, 'package')
		filemap = create_filemap(pkgdir)
		archive = find_archive()
		print('patching archive:', archive)
		update_zip(archive, filemap)
	except Exception as ex:
		print(ex)
		return 1
	else:
		return 0
	finally:
		if SYSTEM == 'Windows':
			print("Press ENTER to continue...")
			sys.stdin.readline()

if __name__ == '__main__':
	sys.exit(main())
