/*
 * mzip.c
 * Iomega Zip/Jaz drive tool
 * change protection mode and eject disk
 */

/* mzip.c by Markus Gyger <mgyger@itr.ch> */
/* This code is based on ftp://gear.torque.net/pub/ziptool.c */
/* by Grant R. Guenther with the following copyright notice: */

/*  (c) 1996   Grant R. Guenther,  based on work of Itai Nahshon  */
/*  http://www.torque.net/ziptool.html  */


/* Unprotect-till-eject modes and mount tests added
 * by Ilya Ovchinnikov <ilya@socio.msu.su>
 */

#include "sysincludes.h"
#include "mtools.h"
#include "scsi.h"

#ifndef _PASSWORD_LEN
#define _PASSWORD_LEN 33
#endif

#ifdef OS_linux
#include <linux/fs.h>
#include <linux/major.h>
#endif

int test_mounted ( char *dev )
{
#ifdef HAVE_MNTENT_H
	struct mntent	*mnt;
	struct stat	st_dev, st_mnt;
	FILE		*mtab;
/*
 * Now check if any partition of this device is already mounted (this
 * includes checking if the device is mounted under a different name).
 */
	
	if (stat (dev, &st_dev)) {
		fprintf (stderr, "%s: stat(%s) failed: %s.\n",
			 progname, dev, strerror (errno));
		exit(1);
	}
	
	if (!S_ISBLK (st_dev.st_mode)) /* not a block device, cannot 
					* be mounted */
		return 0;
	
	if ((mtab = setmntent (MOUNTED, "r")) == NULL) {
		fprintf (stderr, "%s: can't open %s.\n",
			 progname, MOUNTED);
		exit(1);
	}
	
	while ( ( mnt = getmntent (mtab) ) ) {
		if (0
#ifdef MNTTYPE_NFS
		    || !strcmp (mnt->mnt_type, MNTTYPE_NFS)
#endif
#ifdef MNTTYPE_PROC
		    ||  !strcmp (mnt->mnt_type, MNTTYPE_PROC)
#endif
#ifdef MNTTYPE_SMBFS
		    ||  !strcmp (mnt->mnt_type, MNTTYPE_SMBFS)
#endif
#ifdef MNTTYPE_IGNORE
		    ||  !strcmp (mnt->mnt_type, MNTTYPE_IGNORE)
#endif
			)
			continue;
	    
		if (stat (mnt->mnt_fsname, &st_mnt)) {
			fprintf (stderr, "%s: stat(%s) failed: %s.\n",
				 progname, mnt->mnt_fsname, strerror (errno));
			endmntent (mtab);
			exit(1);
		}
		
		if (S_ISBLK (st_mnt.st_mode)) {
#ifdef OS_linux
			if (MAJOR(st_mnt.st_rdev) == SCSI_DISK_MAJOR &&
			    MINOR(st_mnt.st_rdev) >= MINOR(st_dev.st_rdev) &&
			    MINOR(st_mnt.st_rdev) <= MINOR(st_dev.st_rdev)+15) {
				fprintf (stderr, 
					 "Device %s%d is mounted on %s.\n", 
					 dev, MINOR(st_mnt.st_rdev) - MINOR(st_dev.st_rdev),
					 mnt->mnt_dir);
#else
			if(st_mnt.st_rdev != st_dev.st_mode) {
#endif
				endmntent (mtab);
				return 1;
			}
		}
	}
	endmntent (mtab);
#endif
	return 0;
}


static void usage(void)
{
	fprintf(stderr, 
		"Mtools version %s, dated %s\n", 
		mversion, mdate);
	fprintf(stderr, 
		"Usage: %s [-V] [-q] [-e] [-u] [-r|-w|-p|-x] [drive:]\n"
		"\t-q print status\n"
		"\t-e eject disk\n"
		"\t-f eject disk even when mounted\n"
		"\t-r write protected (read-only)\n"
		"\t-w not write-protected (read-write)\n"
		"\t-p password write protected\n"
		"\t-x password protected\n"
		"\t-u unprotect till disk ejecting\n", 
		progname);
	exit(1);
}


enum mode_t {
	ZIP_RW = 0,
	ZIP_RO = 2,
	ZIP_RO_PW = 3,
	ZIP_PW = 5,
	ZIP_UNLOCK_TIL_EJECT = 8
};

static enum mode_t get_zip_status(int fd)
{
	unsigned char status[128];
	unsigned char cdb[6] = { 0x06, 0, 0x02, 0, sizeof status, 0 };
	
	if (scsi_cmd(fd, cdb, 6, SCSI_IO_READ, 
		     status, sizeof status) == -1) {
		perror("status: ");
		exit(1);
	}
	return status[21] & 0xf;
}


static int short_command(int fd, int cmd1, int cmd2, int cmd3, char *data)
{
	unsigned char cdb[6] = { 0, 0, 0, 0, 0, 0 };

	cdb[0] = cmd1;
	cdb[1] = cmd2;
	cdb[4] = cmd3;

	return scsi_cmd(fd, cdb, 6, SCSI_IO_WRITE, 
			data, data ? strlen(data) : 0);
}


static int iomega_command(int fd, int mode, char *data)
{
	return short_command(fd, 
			     SCSI_IOMEGA, mode, data ? strlen(data) : 0,
			     data);
}

static int door_command(int fd, int cmd1, int cmd2)
{
	return short_command(fd, cmd1, 0, cmd2, 0);
}

void mzip(int argc, char **argv, int type)
{
	int c;
	char drive;
	device_t *dev;
	int fd = -1;
	char name[EXPAND_BUF];
	enum { ZIP_NIX    =      0,
	       ZIP_STATUS = 1 << 0,
	       ZIP_EJECT  = 1 << 1,
	       ZIP_MODE_CHANGE = 1 << 2,
	       ZIP_FORCE  = 1 << 3
	} request = ZIP_NIX;

	enum mode_t newMode = ZIP_RW;
	enum mode_t oldMode = ZIP_RW;

#define setMode(x) \
	if(request & ZIP_MODE_CHANGE) usage(); \
	request |= ZIP_MODE_CHANGE; \
	newMode = x; \
	break;
	
	/* get command line options */
	while ((c = getopt(argc, argv, "efpqrwxu")) != EOF) {
		switch (c) {
			case 'f':
				if (get_real_uid()) {
					fprintf(stderr, 
						"Only root can use force. Sorry.\n");
					exit(1);
				}
				request |= ZIP_FORCE;
				break;
			case 'e': /* eject */
				request |= ZIP_EJECT;
				break;
			case 'q': /* status query */
				request |= ZIP_STATUS;
				break;

			case 'p': /* password read-only */
				setMode(ZIP_RO_PW);
			case 'r': /* read-only */
				setMode(ZIP_RO);
			case 'w': /* read-write */
				setMode(ZIP_RW);
			case 'x': /* password protected */
				setMode(ZIP_PW);
			case 'u': /* password protected */
				setMode(ZIP_UNLOCK_TIL_EJECT)
			default:  /* unrecognized */
				usage();
		}
	}
	
	if (request == ZIP_NIX) request = ZIP_STATUS;  /* default action */

	if (argc - optind > 1 || 
	    (argc - optind == 1 &&
	     (!argv[optind][0] || argv[optind][1] != ':')))
		usage();
	
	drive = toupper(argc - optind == 1 ? argv[argc - 1][0] : 'a');
	
	for (dev = devices; dev->name; dev++) {
		unsigned char cdb[6] = { 0, 0, 0, 0, 0, 0 };
		struct {
			char    type,
				type_modifier,
				scsi_version,
				data_format,
				length,
				reserved1[2],
				capabilities,
				vendor[8],
				product[16],
				revision[4],
				vendor_specific[20],
				reserved2[40];
		} inq_data;

		if (dev->drive != drive) 
			continue;
		expand(dev->name, name);
		if ((request & ZIP_MODE_CHANGE) &&
		    !(request & ZIP_FORCE) &&
		    test_mounted(name)) {
			fprintf(stderr, 
				"Can\'t change status of mounted device\n");
			exit(1);
		}
		precmd(dev);

		if(IS_PRIVILEGED(dev))
			reclaim_privs();
		fd = open(name, O_RDONLY | O_NDELAY /* O_RDONLY  | dev->mode*/);
		if(IS_PRIVILEGED(dev))
			drop_privs();

		/* need readonly, else we can't
		 * open the drive on Solaris if
		 * write-protected */		
		if (fd == -1) 
			continue;
		closeExec(fd);

		if (!(request & (ZIP_MODE_CHANGE | ZIP_STATUS)))
			/* if no mode change or ZIP specific status is
			 * involved, the command (eject) is applicable
			 * on all drives */
			break;

		cdb[0] = SCSI_INQUIRY;
		cdb[4] = sizeof inq_data;
		if (scsi_cmd(fd, cdb, 6, SCSI_IO_READ, 
			     &inq_data, sizeof inq_data) != 0) {
			close(fd);
			continue;
		}
		
#ifdef DEBUG
		fprintf(stderr, "device: %s\n\tvendor: %.8s\n\tproduct: %.16s\n"
			"\trevision: %.4s\n", name, inq_data.vendor,
			inq_data.product, inq_data.revision);
#endif /* DEBUG */
		
		if (strncasecmp("IOMEGA  ", inq_data.vendor, 
				sizeof inq_data.vendor) ||
		    (strncasecmp("ZIP 100         ", 
				 inq_data.product, sizeof inq_data.product) &&
		     strncasecmp("JAZ 1GB         ", 
				 inq_data.product, sizeof inq_data.product))) {
			
			/* debugging */
			fprintf(stderr,"Skipping drive with vendor='");
			fwrite(inq_data.vendor,1, sizeof(inq_data.vendor), 
			       stderr);
			fprintf(stderr,"' product='");
			fwrite(inq_data.product,1, sizeof(inq_data.product), 
			       stderr);
			fprintf(stderr,"'\n");
			/* end debugging */
			close(fd);
			continue;
		}
		break;  /* found Zip/Jaz drive */
	}
	
	if (dev->drive == 0) {
		fprintf(stderr, "%s: drive '%c:' is not a Zip or Jaz drive\n",
			argv[0], drive);
		exit(1);
	}
	
	if (request & (ZIP_MODE_CHANGE | ZIP_STATUS))
		oldMode = get_zip_status(fd);

	if (request & ZIP_MODE_CHANGE) {
		/* request temp unlock, and disk is already unlocked */
		if(newMode == ZIP_UNLOCK_TIL_EJECT &&
		   (oldMode & ZIP_UNLOCK_TIL_EJECT))
			request &= ~ZIP_MODE_CHANGE;
		
		/* no password change requested, and disk is already
		 * in the requested state */
		if(!(newMode & 0x01) && newMode == oldMode)
			request &= ~ZIP_MODE_CHANGE;
	}

	if (request & ZIP_MODE_CHANGE) {
		int ret;
		enum mode_t unlockMode, unlockMask;
		char *passwd, dummy[1];

		if(newMode == ZIP_UNLOCK_TIL_EJECT) {
			unlockMode = newMode | oldMode;
			unlockMask = 9;
		} else {
			unlockMode = newMode & ~0x5;
			unlockMask = 1;
		}

		if ((oldMode & unlockMask) == 1) {  /* unlock first */
			char *s, *passwd;
			passwd = "APlaceForYourStuff";
			if ((s = strchr(passwd, '\n'))) *s = '\0';  /* chomp */
			iomega_command(fd, unlockMode, passwd);
		}
		
		if ((get_zip_status(fd) & unlockMask) == 1) { /* unlock first */
			char *s, *passwd;
			passwd = getpass("Password: ");
			if ((s = strchr(passwd, '\n'))) *s = '\0';  /* chomp */
			if((ret=iomega_command(fd, unlockMode, passwd))){
				if (ret == -1) perror("passwd: ");
				else fprintf(stderr, "wrong password\n");
				exit(1);
			}
			if((get_zip_status(fd) & unlockMask) == 1) {
				fprintf(stderr, "wrong password\n");
				exit(1);
			}
		}
		
		if (newMode & 0x1) {
			char first_try[_PASSWORD_LEN];
			
			passwd = getpass("Enter new password:");
			strncpy(first_try, passwd,_PASSWORD_LEN);
			passwd = getpass("Re-type new password:");
			if(strncmp(first_try, passwd, _PASSWORD_LEN)) {
				fprintf(stderr,
					"You mispelled it. Password not set.\n");
				exit(1);
			}
		} else {
			passwd = dummy;
			dummy[0] = '\0';
		}

		if(newMode == ZIP_UNLOCK_TIL_EJECT)
			newMode |= oldMode;

		if((ret=iomega_command(fd, newMode, passwd))){
			if (ret == -1) perror("set passwd: ");
			else fprintf(stderr, "password not changed\n");
			exit(1);
		}
#ifdef OS_linux
		ioctl(fd, BLKRRPART); /* revalidate the disk, so that the
					 kernel notices that its writable
					 status has changed */
#endif
	}
	
	if (request & ZIP_STATUS) {
		char *unlocked;

		if(oldMode & 8)
			unlocked = " and unlocked until eject";
		else
			unlocked = "";		
		switch (oldMode & ~8) {
			case ZIP_RW:  
				printf("Drive '%c:' is not write-protected\n",
				       drive);
				break;
			case ZIP_RO:
				printf("Drive '%c:' is write-protected%s\n",
				       drive, unlocked);
				break;
			case ZIP_RO_PW: 
				printf("Drive '%c:' is password write-protected%s\n", 
				       drive, unlocked);
				break;
			case ZIP_PW:  
				printf("Drive '%c:' is password protected%s\n", 
				       drive, unlocked);
				break;
			default: 
				printf("Unknown protection mode %d of drive '%c:'\n",
				       oldMode, drive);
				break;				
		}		
	}
	
	if (request & ZIP_EJECT) {
		if(request & ZIP_FORCE)
			if(door_command(fd, SCSI_ALLOW_MEDIUM_REMOVAL, 0) < 0) {
				perror("door unlock: ");
				exit(1);
			}

		if(door_command(fd, SCSI_START_STOP, 1) < 0) {
			perror("stop motor: ");
			exit(1);
		}

		if(door_command(fd, SCSI_START_STOP, 2) < 0) {
			perror("eject: ");
			exit(1);
		}
		if(door_command(fd, SCSI_START_STOP, 2) < 0) {
			perror("second eject: ");
			exit(1);
		}
	}
	
	close(fd);
	exit(0);
}
