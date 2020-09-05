package main

import "bytes"
import "crypto/sha256"
import "encoding/base64"
import "errors"
import "fmt"
import "html/template"
import "image/png"
import "io"
import "math/big"
import "mime/multipart"
import "net"
import "net/http"
import "net/http/fcgi"
import "net/url"
import "os"
import "os/exec"
import "strconv"
import "sync"
import "sync/atomic"
import "syscall"
import "time"

var transport *http.Transport = &http.Transport{
	DialContext: (&net.Dialer{
		Timeout: time.Second,
		KeepAlive: -1,
		FallbackDelay: -1,
	}).DialContext,
	IdleConnTimeout: time.Minute,
	TLSHandshakeTimeout: time.Second,
	ExpectContinueTimeout: time.Second,
	DisableKeepAlives: true,
	DisableCompression: true,
	MaxIdleConns: 1000,
	ResponseHeaderTimeout: time.Second,
}
var client http.Client = http.Client{
	Transport: transport,
	CheckRedirect: noRedirects,
	Timeout: time.Second,
}
const encodeStd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
const encodeModulus = len(encodeStd) * len(encodeStd)
var divisor = (&big.Int{}).SetInt64(int64(encodeModulus))
var pageTemplate = template.Must(template.New("compress").Parse(pageMarkup))
const urlPrefix = "/cgi-bin/pngloss/"
var originalsPrefix = "/var/pngloss/"
const maxConcurrentPages = 2
const maxConcurrentImages = 2
const compressedsToCache = 10
var originals = OriginalsOnDisk{}
var compresseds = CompressedsInMemory{}
var compress = PageHandler{}
var compressed = ImageHandler{}

type PageHandler struct {
	lock int32
}

type ImageHandler struct {
	lock int32
}

type OptionalSum struct {
	sum [sha256.Size224]byte
	some bool
}

type OriginalsOnDisk struct {
	lock sync.Mutex
	sums [encodeModulus]OptionalSum
}

type CompressedImage struct {
	sum [sha256.Size224]byte
	strength uint64
	bleed uint64
	strip uint64
	data *[]byte
}

type CompressedsInMemory struct {
	lock sync.Mutex
	compressedIndex int
	compresseds [compressedsToCache]CompressedImage
}

func main() {
	socketPath := "/var/www/run/httpd.sock"
	err := os.Remove(socketPath)
	if err != nil {
		ignore := false
		pathErr, ok := err.(*os.PathError)
		if ok {
			innerErr := pathErr.Unwrap()
			errno, ok := innerErr.(syscall.Errno)
			if ok {
				if errno == syscall.ENOENT {
					ignore = true
				}
			}
		}
		if !ignore {
			fmt.Printf("os.Remove returned %v\n", err)
		}
	}

	address := net.UnixAddr{socketPath, "unix"}
	listener, err := net.ListenUnix("unix", &address);
	if err != nil {
		fmt.Printf("net.ListenUnix returned %v\n", err)
		return
	}

	http.Handle(urlPrefix + "compress.cgi", &compress)
	http.Handle(urlPrefix + "compressed.cgi", &compressed)
	err = fcgi.Serve(listener, nil)
	if err != nil {
		fmt.Printf("fcgi.Serve returned %v\n", err)
		return
	}
}

func (h *PageHandler) ServeHTTP(writer http.ResponseWriter, request *http.Request) {
	inFlight := atomic.AddInt32(&h.lock, 1)
	defer atomic.AddInt32(&h.lock, -1)
	if inFlight > maxConcurrentPages {
		http.Error(writer, "server busy", http.StatusServiceUnavailable)
		return
	}

	if request.Method != "POST" {
		http.Error(writer, "bad method", http.StatusMethodNotAllowed)
		return
	}

	reader, err := request.MultipartReader()
	if err != nil {
		http.Error(writer, "bad form", http.StatusBadRequest)
		return
	}

	maxLengths := map[string]uint{
		"url": 4 * 1024,
		"file": 5 * 1024 * 1024,
		"sum224": 40,
		"strength": 3,
		"bleed": 5,
		"strip": 1,
	}
	buffers := make(map[string][]byte, len(maxLengths))
	for {
		part, err := reader.NextRawPart()
		if err != nil {
			break
		}

		processPart(part, maxLengths, buffers)
	}
	//fmt.Printf("parsed %d parts\n", len(buffers))

	sum := []byte(nil)
	encodedSum := ""
	fileBuffer, ok := buffers["file"]
	if !ok {
		encodedBytes, ok := buffers["sum224"]
		if ok {
			encodedSum = string(encodedBytes)
			sum, err = base64.URLEncoding.DecodeString(encodedSum)
			if err != nil {
				http.Error(writer, "bad encoding", http.StatusBadRequest)
				return
			}

			fileBuffer, err = originals.Load(sum)
			if err != nil {
				http.Error(writer, "unknown sum", http.StatusNotFound)
				return
			}
		} else {
			url, ok := buffers["url"]
			if !ok {
				http.Error(writer, "missing input", http.StatusBadRequest)
				return
			}

			response, err := client.Get(string(url))
			if err != nil {
				//fmt.Printf("http client Get() returned %v\n", err)
				http.Error(writer, "url get failed", http.StatusBadGateway)
				return
			}

			fileBuffer, err = readResponse(response)
			if err != nil {
				//fmt.Printf("readResponse() returned %v\n", err)
				http.Error(writer, "url read failed", http.StatusBadGateway)
				return
			}
		}
	}

	strengthBuffer, ok := buffers["strength"]
	if !ok {
		http.Error(writer, "missing strength", http.StatusBadRequest)
		return
	}
	strength, err := strconv.ParseUint(string(strengthBuffer), 10, 7)
	if err != nil {
		http.Error(writer, "invalid strength", http.StatusBadRequest)
		return
	}

	bleedBuffer, ok := buffers["bleed"]
	if !ok {
		http.Error(writer, "missing bleed", http.StatusBadRequest)
		return
	}
	bleed, err := strconv.ParseUint(string(bleedBuffer), 10, 15)
	if err != nil {
		http.Error(writer, "invalid bleed", http.StatusBadRequest)
		return
	}

	stripBuffer, ok := buffers["strip"]
	if !ok {
		http.Error(writer, "missing strip", http.StatusBadRequest)
		return
	}
	strip, err := strconv.ParseUint(string(stripBuffer), 10, 1)
	if err != nil {
		http.Error(writer, "invalid strip", http.StatusBadRequest)
		return
	}

	fileReader := bytes.NewReader(fileBuffer)
	config, err := png.DecodeConfig(fileReader)
	if err != nil {
		//fmt.Printf("png.DecodeConfig() returned %v\n", err)
		http.Error(writer, "bad png", http.StatusBadRequest)
		return
	}

	//fmt.Printf("width %d, height %d\n", config.Width, config.Height)
	if config.Width > 3000 || config.Height > 3000 {
		fmt.Printf("png too large\n")
		http.Error(writer, "png too large", http.StatusRequestEntityTooLarge)
		return
	}

	sum224 := [sha256.Size224]byte{}
	if sum != nil {
		if len(sum) != sha256.Size224 {
			http.Error(writer, "bad sum", http.StatusBadRequest)
			return
		}
		copy(sum224[:], sum)
	} else {
		sum224 = sha256.Sum224(fileBuffer)
		originals.Save(fileBuffer, sum224)
		encodedSum = base64.URLEncoding.EncodeToString(sum224[:])
	}

	compressed, err := compresseds.Compress(sum224, strength, bleed, strip)


	inSize := len(fileBuffer)
	outSize := len(compressed)
	percent := fmt.Sprintf("%.1f%%", (100 * float32(outSize)) / float32(inSize))

	coefficient := outSize
	unit := "B"
	if (coefficient > 9999) {
		coefficient /= 1000
		unit = "kB"
		if (coefficient > 9999) {
			coefficient /= 1000
			unit = "MB"
			if (coefficient > 9999) {
				coefficient /= 1000
				unit = "GB"
			}
		}
	}
	size := strconv.Itoa(coefficient) + unit

	coefficient = inSize
	unit = "B"
	if (coefficient > 9999) {
		coefficient /= 1000
		unit = "kB"
		if (coefficient > 9999) {
			coefficient /= 1000
			unit = "MB"
			if (coefficient > 9999) {
				coefficient /= 1000
				unit = "GB"
			}
		}
	}
	original := strconv.Itoa(coefficient) + unit

	data := map[string]interface{} {
		"sum224": encodedSum,
		"strength": strength,
		"bleed": bleed,
		"strip": strip,
		"size": size,
		"original": original,
		"percent": percent,
		"width": config.Width,
		"height": config.Height,
	}
	err = pageTemplate.Execute(writer, data)
	if err != nil {
		fmt.Printf("pageTemplate.Execute returned %v\n", err)
		http.Error(writer, "template failed", http.StatusInternalServerError)
		return
	}
}

func (h *ImageHandler) ServeHTTP(writer http.ResponseWriter, request *http.Request) {
	inFlight := atomic.AddInt32(&h.lock, 1)
	defer atomic.AddInt32(&h.lock, -1)

	if inFlight > maxConcurrentImages {
		http.Error(writer, "server busy", http.StatusServiceUnavailable)
		return
	}

	if request.Method != "GET" {
		http.Error(writer, "bad method", http.StatusMethodNotAllowed)
		return
	}

	values, err := url.ParseQuery(request.URL.RawQuery)
	if err != nil {
		http.Error(writer, "bad query", http.StatusBadRequest)
		return
	}

	encodedSum := values.Get("sum224")
	if encodedSum == "" {
		http.Error(writer, "missing sum", http.StatusBadRequest)
		return
	}
	sum, err := base64.URLEncoding.DecodeString(encodedSum)
	if err != nil {
		http.Error(writer, "bad encoding", http.StatusBadRequest)
		return
	}
	if len(sum) != sha256.Size224 {
		http.Error(writer, "bad sum", http.StatusBadRequest)
		return
	}
	sum224 := [sha256.Size224]byte{}
	copy(sum224[:], sum)

	strengthBuffer := values.Get("strength")
	if strengthBuffer == "" {
		http.Error(writer, "missing strength", http.StatusBadRequest)
		return
	}
	strength, err := strconv.ParseUint(strengthBuffer, 10, 7)
	if err != nil {
		http.Error(writer, "invalid strength", http.StatusBadRequest)
		return
	}

	bleedBuffer := values.Get("bleed")
	if bleedBuffer == "" {
		http.Error(writer, "missing bleed", http.StatusBadRequest)
		return
	}
	bleed, err := strconv.ParseUint(string(bleedBuffer), 10, 15)
	if err != nil {
		http.Error(writer, "invalid bleed", http.StatusBadRequest)
		return
	}

	stripBuffer := values.Get("strip")
	if stripBuffer == "" {
		http.Error(writer, "missing strip", http.StatusBadRequest)
		return
	}
	strip, err := strconv.ParseUint(string(stripBuffer), 10, 1)
	if err != nil {
		http.Error(writer, "invalid strip", http.StatusBadRequest)
		return
	}

	data, err := compresseds.Compress(sum224, strength, bleed, strip)
	if err != nil {
		http.Error(writer, "compression failed", http.StatusInternalServerError)
		return
	}

	header := writer.Header()
	header.Set("Content-Type", "image/png")
	writer.Write(data)
}

func processPart(part *multipart.Part, maxLengths map[string]uint, buffers map[string][]byte) {
	defer part.Close()

	name := part.FormName()
	maxLength, ok := maxLengths[name]
	if !ok {
		return
	}

	buffer, ok := buffers[name]
	if ok {
		return
	}

	buffer = make([]byte, maxLength + 1)
	n, err := io.ReadFull(part, buffer)
	if err != io.ErrUnexpectedEOF {
		return
	}

	buffers[name] = buffer[:n]
}

func noRedirects(request *http.Request, via []*http.Request) error {
	return errors.New("redirects prohibited")
}

func readResponse(response *http.Response) ([]byte, error) {
	defer response.Body.Close()
	buffer := make([]byte, 5 * 1024 * 1024 + 1)
	n, err := io.ReadFull(response.Body, buffer)
	if err != io.ErrUnexpectedEOF {
		return nil, errors.New("read failed")
	}

	return buffer[:n], nil
}

func (o *OriginalsOnDisk) Save(data []byte, sum224 [sha256.Size224]byte) {
	mod := ModuloOfSum(sum224[:])
	path := PathForModulo(mod)

	o.lock.Lock()
	defer o.lock.Unlock()

	file, err := os.Create(path)
	_, err = file.Write(data)
	if err != nil {
		os.Remove(path)
		return
	}

	o.sums[mod] = OptionalSum{sum224, true}
}

func (o *OriginalsOnDisk) Load(sum []byte) ([]byte, error) {
	if len(sum) != sha256.Size224 {
		return nil, errors.New("wrong sum size")
	}

	sum224 := [sha256.Size224]byte{}
	copy(sum224[:], sum)

	mod := ModuloOfSum(sum)
	path := PathForModulo(mod)

	o.lock.Lock()
	defer o.lock.Unlock()

	optional := o.sums[mod]
	if optional.some && optional.sum != sum224 {
		return nil, errors.New("wrong sum")
	}

	file, err := os.Open(path)
	if err != nil {
		return nil, errors.New("couldn't open file")
	}

	buffer := make([]byte, 5 * 1024 * 1024 + 1)
	n, err := io.ReadFull(file, buffer)
	if err != io.ErrUnexpectedEOF {
		return nil, errors.New("read failed")
	}

	if !optional.some {
		fileSum := sha256.Sum224(buffer)
		o.sums[mod] = OptionalSum{sum224, true}
		if sum224 != fileSum {
			return nil, errors.New("file has wrong sum")
		}
	}

	return buffer[:n], nil
}

func ModuloOfSum(sum []byte) int {
	bigSum := (&big.Int{}).SetBytes(sum)
	mod := (&big.Int{}).Mod(bigSum, divisor)
	return int(mod.Int64())
}

func PathForModulo(mod int) string {
	path := originalsPrefix
	path += string(encodeStd[mod / len(encodeStd)])
	path += string(encodeStd[mod % len(encodeStd)])
	path += ".png"

	return path
}

func (c *CompressedsInMemory) Load(sum [sha256.Size224]byte, strength uint64, bleed uint64, strip uint64) ([]byte, error) {
	c.lock.Lock()
	defer c.lock.Unlock()

	key := CompressedImage{sum, strength, bleed, strip, nil}
	for _, compressed := range c.compresseds {
		data := compressed.data
		compressed.data = nil
		if compressed == key {
			return *data, nil
		}
	}

	return nil, errors.New("not in memory cache")
}

func (c *CompressedsInMemory) Compress(sum [sha256.Size224]byte, strength uint64, bleed uint64, strip uint64) ([]byte, error) {
	data, err := c.Load(sum, strength, bleed, strip)
	if err == nil {
		return data, nil
	}

	original, err := originals.Load(sum[:])
	if err != nil {
		return nil, err
	}

	args := []string{
		"-s" + strconv.FormatUint(strength, 10),
		"-b" + strconv.FormatUint(bleed, 10),
	}
	if strip != 0 {
		args = append(args, "--strip")
	}
	args = append(args, "-")

	cmd := exec.Command("pngloss", args...)
	cmd.Stdin = bytes.NewReader(original)

	// TODO: try setting niceness of pngloss command with syscall.Setpriority(syscall.PRIO_PROCESS, pid, 20)
	data, err = cmd.Output()
	if err != nil {
		return nil, err
	}

	key := CompressedImage{sum, strength, bleed, strip, &data}

	c.lock.Lock()
	defer c.lock.Unlock()

	compresseds.compresseds[compresseds.compressedIndex] = key
	compresseds.compressedIndex++

	return data, nil
}

var pageMarkup = `<!DOCTYPE html> 
<html lang="en">
  <head>
    <meta name="description" content="Lossy PNG compression to shrink your PNG images"/>
    <title>pngloss</title>
    <link rel="stylesheet" type="text/css" href="/pngloss/style.css">
  </head>
  <body>
    <div class="all-page">
      <form action="/cgi-bin/pngloss/compress.cgi" method="POST" enctype="multipart/form-data">
        <input type="hidden" name="sum224" value="{{.sum224}}">
        <div class="option-box">
          <div class="option-left">
            Quantization Strength:
            <div class="option-left-small">
              (0 - no compression, 85 - max)
            </div>
          </div>
          <div class="option-right">
            <label><input type="number" name="strength" class="radio" value="{{.strength}}" min="0" max="85">
            </label>
          </div>
        </div>
        <div class="option-box">
          <div class="option-left">
            Error Propagation:
            <div class="option-left-small">
              (dithering)
            </div>
          </div>
          <div class="option-right">
            <label><input type="radio" name="bleed" class="radio" value="32767" {{if eq .bleed 32767}} checked {{end}}>
              <span class="r-text">
                None
              </span>
            </label>
            <label><input type="radio" name="bleed" class="radio" value="2" {{if eq .bleed 2}} checked {{end}}>
              <span class="r-text">
                Standard
              </span>
            </label>
            <label><input type="radio" name="bleed" class="radio" value="1" {{if eq .bleed 1}} checked {{end}}>
              <span class="r-text">
                Full
              </span>
            </label>
          </div>
        </div>
        <div class="option-box">
          <div class="option-left">
            Strip Metadata:
          </div>
          <div class="option-right">
            <label><input type="radio" name="strip" class="radio" value="1" {{if .strip}} checked {{end}}>
              <span class="r-text">
                Yes
              </span>
            </label>
            <label><input type="radio" name="strip" class="radio" value="0" {{if not .strip}} checked {{end}}>
              <span class="r-text">
                No
              </span>
            </label>
          </div>
        </div>

        <input id="submit-button" type="submit" class="button" value="Compress Again">
        |
        <a href="/pngloss/" class="blue-link">
          Start Over
        </a>
      </form>
    </div>
    <p class="margin1 link-box">
      Compressed to {{.size}}, {{.percent}} of original {{.original}}.<br>
      <img width="{{.width}}" height="{{.height}}" src="/cgi-bin/pngloss/compressed.cgi?sum224={{.sum224}}&strength={{.strength}}&bleed={{.bleed}}&strip={{.strip}}">
    </p>
    <div class="bottom-info address">
      <a href="https://github.com/foobaz/pngloss" class="gray-link" target="_blank">pngloss</a>
    </div>
  </body>
</html>
`
