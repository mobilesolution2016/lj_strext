#include <stdio.h>
#include <stdlib.h>
#include <string>

#ifdef _WINDOWS
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	include <Shlwapi.h>
#	include <shellapi.h>
#	include <stdint.h>
#else
#   include <sys/time.h>
#   include <sys/types.h>
#   include <sys/ioctl.h>
#   include <sys/uio.h>
#   include <sys/stat.h>
#   include <unistd.h>
#	include <dirent.h>
#	include <errno.h>
#endif

struct LocalDateTime
{
	uint16_t	year, month, day, dayofweek;
	uint16_t	hour, minute, second, millisecond;
};

int deleteDirectory(const char* dir)
{
#ifdef _WINDOWS
	int len = strlen(dir) + 2;
	char tempdirFix[512] = { 0 };
	char* tempdir = tempdirFix;

	if (len > 512)
	{
		tempdir = (char*)malloc(len);
		tempdir[len - 1] = 0;
		tempdir[len] = 0;
	}
	memcpy(tempdir, dir, len);

	SHFILEOPSTRUCTA file_op = {
		NULL,
		FO_DELETE,
		tempdir,
		"",
		FOF_NOCONFIRMATION |
		FOF_NOERRORUI |
		FOF_SILENT,
		false,
		0,
		""
	};
	int ret = SHFileOperationA(&file_op);

	if (tempdir != tempdirFix)
		free(tempdir);

	return ret == 0 ? TRUE : FALSE;
#else
	if (!dir || !dir[0])
		return false;

	DIR* dp = NULL;
	DIR* dpin = NULL;

	struct dirent* dirp;
	dp = opendir(dir);
	if (dp == 0)
		return 0;

	std::string strPathname;
	while ((dirp = readdir(dp)) != 0)
	{
		if (strcmp(dirp->d_name, "..") == 0 || strcmp(dirp->d_name, ".") == 0)
			continue;

		strPathname = dir;
		strPathname += '/';
		strPathname += dirp->d_name;
		dpin = opendir(strPathname.c_str());
		if (dpin != 0)
		{
			closedir(dpin);
			dpin = 0;

			if (!deleteDirectory(strPathname.c_str()))
				return 0;
		}
		else if (remove(strPathname.c_str()) != 0)
		{
			closedir(dp);
			return 0;
		}
	}

	rmdir(dir);
	closedir(dp);

	return 1;
#endif
}

int deleteFile(const char* fname)
{
#ifdef _WINDOWS
	SetFileAttributesA(fname, FILE_ATTRIBUTE_ARCHIVE);
	return DeleteFileA(fname);
#else
	if (access(fname, F_OK) == 0)
		return remove(fname) == 0;
	return false;
#endif
}

bool getFileTime(const char* fname, LocalDateTime* create, LocalDateTime* update)
{
#ifdef _WINDOWS
	HANDLE hFile = CreateFileA(fname, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		SYSTEMTIME sys;
		FILETIME c, u, l;

		BOOL ret = GetFileTime(hFile, &c, 0, &u);
		CloseHandle(hFile);

		if (!ret)
			return false;

		if (create)
		{
			FileTimeToLocalFileTime(&c, &l);
			FileTimeToSystemTime(&l, &sys);
			create->year = sys.wYear;
			create->month = sys.wMonth;
			create->day = sys.wDay;
			create->dayofweek = sys.wDayOfWeek;
			create->hour = sys.wHour;
			create->minute = sys.wMinute;
			create->second = sys.wSecond;
			create->millisecond = sys.wMilliseconds;
		}

		if (update)
		{
			FileTimeToLocalFileTime(&u, &l);
			FileTimeToSystemTime(&l, &sys);
			update->year = sys.wYear;
			update->month = sys.wMonth;
			update->day = sys.wDay;
			update->dayofweek = sys.wDayOfWeek;
			update->hour = sys.wHour;
			update->minute = sys.wMinute;
			update->second = sys.wSecond;
			update->millisecond = sys.wMilliseconds;
		}

		return true;
	}
#else
	struct stat statbuf;
	if (lstat(fname, &statbuf) == 0)
	{
		time_t c;
#ifdef HAVE_ST_BIRTHTIME
		c = statbuf.st_birthtime;
#else
		c = statbuf.st_ctime;
#endif

		if (create)
		{
			tm* ctm = localtime(&c);
			create->year = 1900 + ctm->tm_year;
			create->month = ctm->tm_mon;
			create->day = ctm->tm_mday;
			create->dayofweek = ctm->tm_wday;
			create->hour = ctm->tm_hour;
			create->minute = ctm->tm_min;
			create->second = ctm->tm_sec;
			create->millisecond = 0;
		}

		if (update)
		{
			tm* utm = localtime((const time_t *)&statbuf.st_mtime);
			update->year = 1900 + utm->tm_year;
			update->month = utm->tm_mon;
			update->day = utm->tm_mday;
			update->dayofweek = utm->tm_wday;
			update->hour = utm->tm_hour;
			update->minute = utm->tm_min;
			update->second = utm->tm_sec;
			update->millisecond = 0;
		}

		return true;
	}
#endif
	return false;
}

double getFileSize(const char* fname)
{
#ifdef _WINDOWS
	HANDLE hFile = CreateFileA(fname, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile && hFile != INVALID_HANDLE_VALUE)
	{
		DWORD hi;
		DWORD lo = GetFileSize(hFile, &hi);
		CloseHandle(hFile);

		return static_cast<double>(((uint64_t)hi << 32) | lo);
	}
#else
	FILE* fp = fopen(fname, "rb");
	if (fp)
	{
		fseek(fp, 0L, SEEK_END);
		long s = ftell(fp);
		fclose(fp);

		return static_cast<double>(s);
	}
#endif

	return -1;
}

#ifndef _WINDOWS
bool fnamematch(const char* needle, const char* haystack)
{
	for (; needle[0] != '\0'; ++ needle)
	{
		switch (needle[0])
		{
		case '?':
			++ haystack;
			break;

		case '*':
		{
			if (needle[1] == '\0' || haystack[0] == '\0')
				return true;
			for (size_t i = 0; ; ++ i)
			{
				if (haystack[i] == '\0')
					return false;
				if (fnamematch(needle + 1, haystack + i))
					return true;
			}
			return false;
		}
		break;

		default:
			if (haystack[0] != needle[0])
				return false;
			++ haystack;
			break;
		}
	}

	return haystack[0] == '\0';
}
const char* readdirinfo(void* p, const char* filter)
{
	for (;;)
	{
		const dirent* d = readdir((DIR*)p);
		if (!d)
			break;

		const char* name = d->d_name;
		if (!filter || strcmp(filter, "*.*") == 0)
			return name;
		if (!filter[1] && (filter[0] == '.' || filter[0] == '*'))
			return name;

		if (fnamematch(filter, name))
			return name;
	}
	return NULL;
}

unsigned getpathattrs(const char* path)
{
	struct stat buf;
	if (stat(path, &buf) == -1)
		return 0;

	unsigned r = 0;
	if (S_ISREG(buf.st_mode)) r |= 1;
	if (S_ISDIR(buf.st_mode)) r |= 2;
	if (buf.st_mode & S_IFLNK) r |= 4;
	if (buf.st_mode & S_IFSOCK) r |= 8;

	const char* lastseg = strrchr(path, '/');
	if (lastseg)
	{
		if (lastseg + 1 <= path && lastseg[1] == '.')
			r |= 0x10;
	}
	else if (path[0])
	{
		r |= 0x10;
	}

	return r | (buf.st_mode & 0x1FF);
}

bool pathisfile(const char* path)
{
	struct stat buf;
	if (stat(path, &buf) != 0)
		return 0;
	return S_ISREG(buf.st_mode);
}
bool pathisdir(const char* path)
{
	struct stat buf = { 0 };
	if (stat(path, &buf) != 0)
		return 0;
	return S_ISDIR(buf.st_mode);
}
unsigned pathisexists(const char* path)
{
	struct stat buf = { 0 };
	if (stat(path, &buf) != 0)
		return 0;

	if (S_ISREG(buf.st_mode))
		return 1;
	if (S_ISDIR(buf.st_mode))
		return 2;
	return 0;
}
bool createdir(const char* path, int mode)
{
	struct stat buf = { 0 };
	if (stat(path, &buf) == -1)
	{
		umask(0);
		return mkdir(path, mode != 0 ? mode : 0666) == 0 || errno == EEXIST;
	}

	return (S_ISDIR(buf.st_mode)) ? true : false;
}
#endif