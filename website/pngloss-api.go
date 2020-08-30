package main

import "bytes"
import "errors"
import "fmt"
import "image/png"
import "io"
import "mime/multipart"
import "net"
import "net/http"
import "net/http/fcgi"
import "os"
import "os/exec"
import "strconv"
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

type PnglossHandler struct {
	lock int32
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

	handler := PnglossHandler{}
	err = fcgi.Serve(listener, &handler)
	if err != nil {
		fmt.Printf("fcgi.Serve returned %v\n", err)
		return
	}
}

func (p *PnglossHandler) ServeHTTP(writer http.ResponseWriter, request *http.Request) {
	inFlight := atomic.AddInt32(&p.lock, 1)
	defer atomic.AddInt32(&p.lock, -1)
	if inFlight > 1 {
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

	fileBuffer, ok := buffers["file"]
	if !ok {
		url, ok := buffers["url"]
		if !ok {
			http.Error(writer, "request must include file or url", http.StatusBadRequest)
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

	if config.Width > 3000 || config.Height > 3000 {
		http.Error(writer, "png too large", http.StatusBadRequest)
		return
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
	cmd.Stdin = bytes.NewReader(fileBuffer)

	out, err := cmd.Output()
	if err != nil {
		http.Error(writer, "failed to run", http.StatusBadRequest)
		return
	}

	header := writer.Header()
	header.Set("Content-Type", "image/png")
	writer.Write(out)
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
