/*****************************************************************************
#                                                                            #
#    KVMD-FAN - A small fan controller daemon for PiKVM.                     #
#                                                                            #
#    Copyright (C) 2018-2022  Maxim Devaev <mdevaev@gmail.com>               #
#                                                                            #
#    This program is free software: you can redistribute it and/or modify    #
#    it under the terms of the GNU General Public License as published by    #
#    the Free Software Foundation, either version 3 of the License, or       #
#    (at your option) any later version.                                     #
#                                                                            #
#    This program is distributed in the hope that it will be useful,         #
#    but WITHOUT ANY WARRANTY; without even the implied warranty of          #
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           #
#    GNU General Public License for more details.                            #
#                                                                            #
#    You should have received a copy of the GNU General Public License       #
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.  #
#                                                                            #
*****************************************************************************/


#include "temp.h"


int get_temp(float *temp) {
	int retval = 0;

	FILE *fp = NULL;
	if ((fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r")) == NULL) {
		LOG_PERROR("temp", "Can't read thermal zone 0");
		goto error;
	}

	int raw;
	if (fscanf(fp, "%d", &raw) != 1) {
		LOG_ERROR("temp", "Can't parse thermal zone 0");
		goto error;
	}
	*temp = (float)raw / 1000;

	goto ok;
	error:
		retval = -1;
	ok:
		if (fp) {
			fclose(fp);
		}
		return retval;
}
