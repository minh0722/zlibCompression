int main()
{
    HANDLE test = CreateFile(
        L"DELETETHIS.bin",
	    GENERIC_READ | GENERIC_WRITE,
	    0,
		nullptr,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);

		LARGE_INTEGER i;
		i.QuadPart = 4294967296;

		// set pointer of file to the desired size
		auto ret = SetFilePointer(test, i.LowPart, &i.HighPart, FILE_BEGIN);
	 	assert(ret != INVALID_SET_FILE_POINTER);

		// then extend the file by set the file pointer to the end
		// the content of the file is undefined
		// from msdn: https://msdn.microsoft.com/en-us/library/windows/desktop/aa365531(v=vs.85).aspx
		SetEndOfFile(test);
		CloseHandle(test);

    return 0;
}
