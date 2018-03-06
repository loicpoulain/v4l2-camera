#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#define BUF_NUM 2
#define BUF_SIZE 4000000 /* 4M */

#define S_TO_US(s) ((s) * 1000 * 1000)
#define NS_TO_US(us) ((us) / 1000)

int main(int argc, char *argv[])
{
	struct v4l2_capability caps = {0};
	struct v4l2_fmtdesc fmtdesc = {0};
	struct v4l2_format fmt = {0};
	struct v4l2_requestbuffers breq = {0};
	struct v4l2_buffer buf[BUF_NUM];
	int fd, err, i, last_seq, pixelfmt = 0, pixelfmt_idx = 0;
	unsigned long missed = 0;

	/* Open capture device */
	if (argc >= 2)
		fd = open(argv[1], O_RDONLY);
	else
		fd = open("/dev/video0", O_RDONLY);
	if (fd < 0) {
		perror("Unable to open video device");
		return -1;
	}

	/* Format index to use ? */
	if (argc >= 3)
		pixelfmt_idx = atoi(argv[2]);

	/* Request device capabilities */
	err = ioctl(fd, VIDIOC_QUERYCAP, &caps);
	if (err) {
		perror("Unable to request capabilities");
		goto error;
	}

	/* Check this is a capture device */
	if (!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, "Not a capture device\n");
		goto error;
	}

	/* Check formats */
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	printf("Formats:\n");
	while (!ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc)) {
		printf("%d: %s", fmtdesc.index, fmtdesc.description);
		if (fmtdesc.flags & V4L2_FMT_FLAG_COMPRESSED)
			printf(" (compressed)");
		if (fmtdesc.flags & V4L2_FMT_FLAG_EMULATED)
			printf(" (emulated)");
		if (fmtdesc.index == pixelfmt_idx) {
			pixelfmt = fmtdesc.pixelformat;
			printf(" < SELECTED");
		}
		printf("\n");
		fmtdesc.index++;
	}

	if (pixelfmt_idx >= fmtdesc.index) {
		fprintf(stderr, "Invalid format selected\n");
	}

	/* Set Format */
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = 640;
	fmt.fmt.pix.height = 480;
	fmt.fmt.pix.pixelformat = pixelfmt;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;
	err = ioctl(fd, VIDIOC_S_FMT, &fmt);
	if (err) {
		perror("Unable to set format");
		goto error;
	}

	/* Request buffer */
	breq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	breq.memory = V4L2_MEMORY_USERPTR;
	breq.count = BUF_NUM; /* not really requested for userptr */
	err = ioctl(fd, VIDIOC_REQBUFS, &breq);
	if (err) {
		perror("Unable to request buffer");
		goto error;
	}

	/* alloc buffers and enqueue */
	for (i = 0; i < BUF_NUM; i++) {
		buf[i].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf[i].memory = V4L2_MEMORY_USERPTR;
		buf[i].index = i;
		buf[i].length = BUF_SIZE;
		buf[i].m.userptr = (unsigned long)calloc(1, BUF_SIZE);

		err = ioctl(fd, VIDIOC_QBUF, &buf[i]);
		if (err) {
			perror("Unable to enqueue buffer");
			goto error;
		}
	}

	/* Start the stream */
	err = ioctl(fd, VIDIOC_STREAMON, &breq.type);
	if (err) {
		perror("Unable to start streaming");
		goto error;
	}

	setbuf(stdout, NULL);
	i = 0; last_seq = 0;
	do {
		struct v4l2_buffer vbuf = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_USERPTR,
		};

		err = ioctl(fd, VIDIOC_DQBUF, &vbuf);
		if (err) {
			perror("unable to dequeue buf");
		}

		for (i = last_seq + 1; i < vbuf.sequence ; i++) {
			missed++; /* frame missed */
		}

		last_seq = vbuf.sequence;

		printf("\rFrame %u, missed %ld", vbuf.sequence, missed);

		/* We've done with the buffer, requeue */
		err = ioctl(fd, VIDIOC_QBUF, &vbuf);
		if (err) {
			perror("unable to enqueue buffer");
		}
	} while (!err);

	/* Stop the stream */
	err = ioctl(fd, VIDIOC_STREAMOFF, &breq.type);
	if (err) {
		perror("Unable to stop streaming");
		goto error;
	}

	close(fd);
	return 0;
error:
	close(fd);
	return -1;
}
