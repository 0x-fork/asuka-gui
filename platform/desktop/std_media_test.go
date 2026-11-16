package desktop_test

import (
	"bytes"
	"encoding/binary"
	"image"
	"image/color"
	"image/jpeg"
	"image/png"
)

// testPNG is a w×h PNG: its left half opaque red, its right half clear.
func testPNG(w, h int) []byte {
	img := image.NewNRGBA(image.Rect(0, 0, w, h))
	for y := 0; y < h; y++ {
		for x := 0; x < w/2; x++ {
			img.SetNRGBA(x, y, color.NRGBA{255, 0, 0, 255})
		}
	}
	var b bytes.Buffer
	png.Encode(&b, img)
	return b.Bytes()
}

// orientedJPEG is a w×h JPEG whose EXIF orientation is o (6: turned a
// quarter clockwise to be upright).
func orientedJPEG(w, h int, o uint16) []byte {
	img := image.NewRGBA(image.Rect(0, 0, w, h))
	for i := range img.Pix {
		img.Pix[i] = 200
	}
	var b bytes.Buffer
	jpeg.Encode(&b, img, nil)
	j := b.Bytes()
	// APP1: Exif, a big-endian TIFF with one IFD entry, Orientation.
	tiff := []byte("MM\x00\x2a\x00\x00\x00\x08")
	tiff = append(tiff, 0, 1)                         // one entry
	tiff = append(tiff, 0x01, 0x12, 0, 3, 0, 0, 0, 1) // Orientation, SHORT, 1
	tiff = binary.BigEndian.AppendUint16(tiff, o)
	tiff = append(tiff, 0, 0, 0, 0, 0, 0) // padding, no next IFD
	seg := append([]byte("Exif\x00\x00"), tiff...)
	app1 := []byte{0xFF, 0xE1}
	app1 = binary.BigEndian.AppendUint16(app1, uint16(len(seg)+2))
	app1 = append(app1, seg...)
	out := append([]byte{0xFF, 0xD8}, app1...)
	return append(out, j[2:]...)
}

// mjpegAVI is a video of n w×h frames at fps, each a JPEG (Motion JPEG
// in AVI: what GStreamer's avidemux and jpegdec read).
func mjpegAVI(w, h, n, fps int) []byte {
	var frames [][]byte
	for i := 0; i < n; i++ {
		img := image.NewRGBA(image.Rect(0, 0, w, h))
		for p := 0; p < len(img.Pix); p += 4 {
			img.Pix[p], img.Pix[p+1], img.Pix[p+2], img.Pix[p+3] = byte(i*20), 100, 200, 255
		}
		var b bytes.Buffer
		jpeg.Encode(&b, img, nil)
		frames = append(frames, b.Bytes())
	}
	le32 := func(b []byte, v int) []byte { return binary.LittleEndian.AppendUint32(b, uint32(v)) }
	le16 := func(b []byte, v int) []byte { return binary.LittleEndian.AppendUint16(b, uint16(v)) }
	chunk := func(id string, data []byte) []byte {
		b := le32([]byte(id), len(data))
		b = append(b, data...)
		if len(data)%2 == 1 {
			b = append(b, 0)
		}
		return b
	}
	list := func(kind string, data []byte) []byte { return chunk("LIST", append([]byte(kind), data...)) }
	biggest := 0
	for _, f := range frames {
		biggest = max(biggest, len(f))
	}
	var avih []byte
	avih = le32(avih, 1000000/fps) // µs per frame
	avih = le32(avih, biggest*fps)
	avih = le32(avih, 0)
	avih = le32(avih, 0x10) // AVIF_HASINDEX
	avih = le32(avih, n)
	avih = le32(avih, 0)
	avih = le32(avih, 1) // streams
	avih = le32(avih, biggest)
	avih = le32(avih, w)
	avih = le32(avih, h)
	avih = append(avih, make([]byte, 16)...)
	var strh []byte
	strh = append(strh, "vidsMJPG"...)
	strh = le32(strh, 0) // flags
	strh = le16(strh, 0)
	strh = le16(strh, 0)
	strh = le32(strh, 0) // initial frames
	strh = le32(strh, 1) // scale
	strh = le32(strh, fps)
	strh = le32(strh, 0) // start
	strh = le32(strh, n) // length
	strh = le32(strh, biggest)
	strh = le32(strh, -1&0xffffffff) // quality
	strh = le32(strh, 0)             // sample size
	strh = le16(strh, 0)
	strh = le16(strh, 0)
	strh = le16(strh, w)
	strh = le16(strh, h)
	var strf []byte // BITMAPINFOHEADER
	strf = le32(strf, 40)
	strf = le32(strf, w)
	strf = le32(strf, h)
	strf = le16(strf, 1)
	strf = le16(strf, 24)
	strf = append(strf, "MJPG"...)
	strf = le32(strf, w*h*3)
	strf = append(strf, make([]byte, 16)...)
	hdrl := list("hdrl", append(chunk("avih", avih), list("strl", append(chunk("strh", strh), chunk("strf", strf)...))...))
	var movi, idx []byte
	off := 4
	for _, f := range frames {
		c := chunk("00dc", f)
		idx = append(idx, "00dc"...)
		idx = le32(idx, 0x10) // AVIIF_KEYFRAME
		idx = le32(idx, off)
		idx = le32(idx, len(f))
		movi = append(movi, c...)
		off += len(c)
	}
	body := append([]byte("AVI "), hdrl...)
	body = append(body, list("movi", movi)...)
	body = append(body, chunk("idx1", idx)...)
	return chunk("RIFF", body)
}
