#include <u.h>
#include <libc.h>
#include <bio.h>
#include <auth.h>
#include <mp.h>
#include <libsec.h>

// The main groups of functions are:
//		client/server - main handshake protocol definition
//		message functions - formating handshake messages
//		cipher choices - catalog of digest and encrypt algorithms
//		security functions - PKCS#1, sslHMAC, session keygen
//		general utility functions - malloc, serialization
// The handshake protocol builds on the TLS/SSL3 record layer protocol,
// which is implemented in kernel device #a.  See also /lib/rfc/rfc2246.

enum {
	TLSFinishedLen = 12,
	SSL3FinishedLen = MD5dlen+SHA1dlen,
	MaxKeyData = 136,	// amount of secret we may need
	MaxChunk = 1<<14,
	RandomSize = 32,
	SidSize = 32,
	MasterSecretSize = 48,
	AQueue = 0,
	AFlush = 1,
};

typedef struct TlsSec TlsSec;

typedef struct Bytes{
	int len;
	uchar data[1];  // [len]
} Bytes;

typedef struct Ints{
	int len;
	int data[1];  // [len]
} Ints;

typedef struct Algs{
	char *enc;
	char *digest;
	int nsecret;
	int tlsid;
	int sha384;	// use the sha384 PRF and finished hash
	int ok;
} Algs;

typedef struct Namedcurve{
	int tlsid;
	void (*init)(mpint *p, mpint *a, mpint *b, mpint *x, mpint *y, mpint *n, mpint *h);
} Namedcurve;

typedef struct Finished{
	uchar verify[SSL3FinishedLen];
	int n;
} Finished;

typedef struct HandHash{
	MD5state	md5;
	SHAstate	sha1;
	SHA2_256state	sha2_256;
	SHA2_384state	sha2_384;
} HandHash;

typedef struct TlsConnection{
	TlsSec *sec;	// security management goo
	int hand, ctl;	// record layer file descriptors
	int erred;		// set when tlsError called
	int (*trace)(char*fmt, ...); // for debugging
	int version;	// protocol we are speaking
	int verset;		// version has been set
	int ver2hi;		// server got a version 2 hello
	int isClient;	// is this the client or server?
	Bytes *sid;		// SessionID
	Bytes *cert;	// only last - no chain

	Lock statelk;
	int state;		// must be set using setstate

	// input buffer for handshake messages
	uchar buf[MaxChunk+8*1024];
	uchar *rp, *ep;

	uchar crandom[RandomSize];	// client random
	uchar srandom[RandomSize];	// server random
	int clientVersion;	// version in ClientHello
	char *digest;	// name of digest algorithm to use
	char *enc;		// name of encryption algorithm to use
	int nsecret;	// amount of secret data to init keys
	int sha384;		// cipher suite uses the sha384 PRF
	int cipher;		// negotiated cipher suite
	int curve;		// ephemeral key exchange group chosen by the server
	Bytes *Ys;		// server ephemeral key exchange public value

	// for finished messages
	HandHash	hs;	// handshake hash
	Finished	finished;
} TlsConnection;

typedef struct Msg{
	int tag;
	union {
		struct {
			int version;
			uchar 	random[RandomSize];
			Bytes*	sid;
			Ints*	ciphers;
			Bytes*	compressors;
			Ints*	sigAlgs;
			Ints*	curves;
			char*	serverName;
			int	secReneg;
		} clientHello;
		struct {
			int version;
			uchar 	random[RandomSize];
			Bytes*	sid;
			int cipher;
			int compressor;
			int secReneg;
		} serverHello;
		struct {
			int ncert;
			Bytes **certs;
		} certificate;
		struct {
			Bytes *types;
			int nca;
			Bytes **cas;
		} certificateRequest;
		struct {
			int curve;
			Bytes *key;
			int sigalg;
			Bytes *signature;
		} serverKeyExchange;
		struct {
			Bytes *key;
		} clientKeyExchange;
		Finished finished;
	} u;
} Msg;

typedef struct TlsSec{
	char *server;	// name of remote; nil for server
	int ok;	// <0 killed; == 0 in progress; >0 reusable
	RSApub *rsapub;
	AuthRpc *rpc;	// factotum for rsa private key
	uchar sec[MasterSecretSize];	// master secret
	uchar crandom[RandomSize];	// client random
	uchar srandom[RandomSize];	// server random
	int clientVers;		// version in ClientHello
	int vers;			// final version
	int sha384;			// use the sha384 PRF and finished hash
	// ephemeral elliptic curve diffie-hellman state
	Namedcurve *nc;		// selected curve
	struct {
		ECdomain dom;
		ECpriv Q;
	} ec;
	uchar X[32];		// x25519 scalar
	DHstate dh;			// finite-field diffie-hellman state
	// byte generation and handshake checksum
	void (*prf)(uchar*, int, uchar*, int, char*, uchar*, int, uchar*, int);
	void (*setFinished)(TlsSec*, HandHash, uchar*, int);
	int nfin;
} TlsSec;


enum {
	SSL3Version  = 0x0300,
	TLS10Version = 0x0301,
	TLS11Version = 0x0302,
	TLS12Version = 0x0303,
	ProtocolVersion = TLS12Version,	// maximum version we speak
	MinProtoVersion = 0x0300,	// limits on version we accept
	MaxProtoVersion	= 0x03ff,
};

// handshake type
enum {
	HHelloRequest,
	HClientHello,
	HServerHello,
	HSSL2ClientHello = 9,  /* local convention;  see devtls.c */
	HCertificate = 11,
	HServerKeyExchange,
	HCertificateRequest,
	HServerHelloDone,
	HCertificateVerify,
	HClientKeyExchange,
	HFinished = 20,
	HMax
};

// alerts
enum {
	ECloseNotify = 0,
	EUnexpectedMessage = 10,
	EBadRecordMac = 20,
	EDecryptionFailed = 21,
	ERecordOverflow = 22,
	EDecompressionFailure = 30,
	EHandshakeFailure = 40,
	ENoCertificate = 41,
	EBadCertificate = 42,
	EUnsupportedCertificate = 43,
	ECertificateRevoked = 44,
	ECertificateExpired = 45,
	ECertificateUnknown = 46,
	EIllegalParameter = 47,
	EUnknownCa = 48,
	EAccessDenied = 49,
	EDecodeError = 50,
	EDecryptError = 51,
	EExportRestriction = 60,
	EProtocolVersion = 70,
	EInsufficientSecurity = 71,
	EInternalError = 80,
	EInappropriateFallback = 86,
	EUserCanceled = 90,
	ENoRenegotiation = 100,
	EMax = 256
};

// cipher suites
enum {
	TLS_NULL_WITH_NULL_NULL	 		= 0x0000,
	TLS_RSA_WITH_NULL_MD5 			= 0x0001,
	TLS_RSA_WITH_NULL_SHA 			= 0x0002,
	TLS_RSA_EXPORT_WITH_RC4_40_MD5 		= 0x0003,
	TLS_RSA_WITH_RC4_128_MD5 		= 0x0004,
	TLS_RSA_WITH_RC4_128_SHA 		= 0x0005,
	TLS_RSA_EXPORT_WITH_RC2_CBC_40_MD5	= 0X0006,
	TLS_RSA_WITH_IDEA_CBC_SHA 		= 0X0007,
	TLS_RSA_EXPORT_WITH_DES40_CBC_SHA	= 0X0008,
	TLS_RSA_WITH_DES_CBC_SHA		= 0X0009,
	TLS_RSA_WITH_3DES_EDE_CBC_SHA		= 0X000A,
	TLS_DH_DSS_EXPORT_WITH_DES40_CBC_SHA	= 0X000B,
	TLS_DH_DSS_WITH_DES_CBC_SHA		= 0X000C,
	TLS_DH_DSS_WITH_3DES_EDE_CBC_SHA	= 0X000D,
	TLS_DH_RSA_EXPORT_WITH_DES40_CBC_SHA	= 0X000E,
	TLS_DH_RSA_WITH_DES_CBC_SHA		= 0X000F,
	TLS_DH_RSA_WITH_3DES_EDE_CBC_SHA	= 0X0010,
	TLS_DHE_DSS_EXPORT_WITH_DES40_CBC_SHA	= 0X0011,
	TLS_DHE_DSS_WITH_DES_CBC_SHA		= 0X0012,
	TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA	= 0X0013,	// ZZZ must be implemented for tls1.0 compliance
	TLS_DHE_RSA_EXPORT_WITH_DES40_CBC_SHA	= 0X0014,
	TLS_DHE_RSA_WITH_DES_CBC_SHA		= 0X0015,
	TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA	= 0X0016,
	TLS_DH_anon_EXPORT_WITH_RC4_40_MD5	= 0x0017,
	TLS_DH_anon_WITH_RC4_128_MD5 		= 0x0018,
	TLS_DH_anon_EXPORT_WITH_DES40_CBC_SHA	= 0X0019,
	TLS_DH_anon_WITH_DES_CBC_SHA		= 0X001A,
	TLS_DH_anon_WITH_3DES_EDE_CBC_SHA	= 0X001B,

	TLS_RSA_WITH_AES_128_CBC_SHA		= 0X002f,	// aes, aka rijndael with 128 bit blocks
	TLS_DH_DSS_WITH_AES_128_CBC_SHA		= 0X0030,
	TLS_DH_RSA_WITH_AES_128_CBC_SHA		= 0X0031,
	TLS_DHE_DSS_WITH_AES_128_CBC_SHA	= 0X0032,
	TLS_DHE_RSA_WITH_AES_128_CBC_SHA	= 0X0033,
	TLS_DH_anon_WITH_AES_128_CBC_SHA	= 0X0034,
	TLS_RSA_WITH_AES_256_CBC_SHA		= 0X0035,
	TLS_DH_DSS_WITH_AES_256_CBC_SHA		= 0X0036,
	TLS_DH_RSA_WITH_AES_256_CBC_SHA		= 0X0037,
	TLS_DHE_DSS_WITH_AES_256_CBC_SHA	= 0X0038,
	TLS_DHE_RSA_WITH_AES_256_CBC_SHA	= 0X0039,
	TLS_DH_anon_WITH_AES_256_CBC_SHA	= 0X003A,
	TLS_RSA_WITH_AES_128_CBC_SHA256		= 0X003C,
	TLS_RSA_WITH_AES_256_CBC_SHA256		= 0X003D,
	TLS_RSA_WITH_AES_128_GCM_SHA256		= 0X009C,
	TLS_RSA_WITH_AES_256_GCM_SHA384		= 0X009D,
	TLS_DHE_RSA_WITH_AES_128_CBC_SHA256	= 0X0067,
	TLS_DHE_RSA_WITH_AES_128_GCM_SHA256	= 0X009E,
	TLS_EMPTY_RENEGOTIATION_INFO_SCSV	= 0x00FF,
	TLS_FALLBACK_SCSV			= 0x5600,
	CipherMax,

	TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA	= 0xC013,
	TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA	= 0xC014,
	TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256	= 0xC027,
	TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256	= 0xC02F,
	TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384	= 0xC030,
	TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256	= 0xCCA8,
	TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA	= 0xC009,
	TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA	= 0xC00A,
	TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256	= 0xC023,
	TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256	= 0xC02B,
	TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384	= 0xC02C,
	TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305	= 0xCCA9,
	TLS_DHE_RSA_WITH_CHACHA20_POLY1305	= 0xCCAA,
};

// compression methods
enum {
	CompressionNull = 0,
	CompressionMax
};

// extensions
enum {
	ExtServerName = 0,
	ExtEllipticCurves = 0x000a,	// supported_groups
	ExtPointFormats = 0x000b,
	ExtSigalgs = 0xd,
	ExtRenegInfo = 0xff01,
};

// elliptic curves (named groups)
enum {
	X25519 = 0x001d,
	secp256r1Curve = 0x0017,
	secp384r1Curve = 0x0018,
};

// signature algorithms
enum {
	RSA_PKCS1_SHA1   = 0x0201,
	RSA_PKCS1_SHA256 = 0x0401,
	RSA_PKCS1_SHA384 = 0x0501,
	RSA_PKCS1_SHA512 = 0x0601,
	ECDSA_SECP256R1_SHA256 = 0x0403,
};

static Algs cipherAlgs[] = {
	{"ccpoly96_aead", "clear", 2*(32+12), TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305},
	{"aes_128_gcm_aead", "clear", 2*(16+4), TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256},
	{"aes_256_gcm_aead", "clear", 2*(32+4), TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, 1},
	{"aes_128_cbc", "sha256", 2*(16+16+SHA2_256dlen), TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256},
	{"aes_128_cbc", "sha1", 2*(16+16+SHA1dlen), TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA},
	{"aes_256_cbc", "sha1", 2*(32+16+SHA1dlen), TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA},
	{"ccpoly96_aead", "clear", 2*(32+12), TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256},
	{"aes_128_gcm_aead", "clear", 2*(16+4), TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256},
	{"aes_256_gcm_aead", "clear", 2*(32+4), TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384, 1},
	{"aes_128_cbc", "sha256", 2*(16+16+SHA2_256dlen), TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256},
	{"aes_128_cbc", "sha1", 2*(16+16+SHA1dlen), TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA},
	{"aes_256_cbc", "sha1", 2*(32+16+SHA1dlen), TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA},
	{"ccpoly96_aead", "clear", 2*(32+12), TLS_DHE_RSA_WITH_CHACHA20_POLY1305},
	{"aes_128_gcm_aead", "clear", 2*(16+4), TLS_DHE_RSA_WITH_AES_128_GCM_SHA256},
	{"aes_128_cbc", "sha256", 2*(16+16+SHA2_256dlen), TLS_DHE_RSA_WITH_AES_128_CBC_SHA256},
	{"aes_128_cbc", "sha1", 2*(16+16+SHA1dlen), TLS_DHE_RSA_WITH_AES_128_CBC_SHA},
	{"aes_256_cbc", "sha1", 2*(32+16+SHA1dlen), TLS_DHE_RSA_WITH_AES_256_CBC_SHA},
	{"3des_ede_cbc", "sha1", 2*(4*8+SHA1dlen), TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA},
	{"rc4_128", "md5", 2*(16+MD5dlen), TLS_RSA_WITH_RC4_128_MD5},
	{"rc4_128", "sha1", 2*(16+SHA1dlen), TLS_RSA_WITH_RC4_128_SHA},
	{"3des_ede_cbc", "sha1", 2*(4*8+SHA1dlen), TLS_RSA_WITH_3DES_EDE_CBC_SHA},
	{"aes_128_cbc", "sha1", 2*(16+16+SHA1dlen), TLS_RSA_WITH_AES_128_CBC_SHA},
	{"aes_256_cbc", "sha1", 2*(32+16+SHA1dlen), TLS_RSA_WITH_AES_256_CBC_SHA},
	{"aes_128_gcm_aead", "clear", 2*(16+4), TLS_RSA_WITH_AES_128_GCM_SHA256},
	{"aes_256_gcm_aead", "clear", 2*(32+4), TLS_RSA_WITH_AES_256_GCM_SHA384, 1},
	{"aes_128_cbc", "sha256", 2*(16+16+SHA2_256dlen), TLS_RSA_WITH_AES_128_CBC_SHA256},
	{"aes_256_cbc", "sha256", 2*(32+16+SHA2_256dlen), TLS_RSA_WITH_AES_256_CBC_SHA256}
};

static uchar compressors[] = {
	CompressionNull,
};

static int sigAlgs[] = {
	ECDSA_SECP256R1_SHA256,
	RSA_PKCS1_SHA256,
	RSA_PKCS1_SHA1,
};

static Namedcurve namedcurves[] = {
	{X25519, nil},
	{secp256r1Curve, secp256r1},
	{secp384r1Curve, secp384r1},
};

static int tlscurves[] = {
	X25519,
	secp256r1Curve,
	secp384r1Curve,
};

static TlsConnection *tlsServer2(int ctl, int hand, uchar *cert, int ncert, int (*trace)(char*fmt, ...), PEMChain *chain);
static TlsConnection *tlsClient2(int ctl, int hand, uchar *csid, int ncsid, char *serverName, int (*trace)(char*fmt, ...));

static void	msgClear(Msg *m);
static char* msgPrint(char *buf, int n, Msg *m);
static int	msgRecv(TlsConnection *c, Msg *m);
static int	msgSend(TlsConnection *c, Msg *m, int act);
static void	tlsError(TlsConnection *c, int err, char *msg, ...);
#pragma	varargck argpos	tlsError 3
static int setVersion(TlsConnection *c, int version);
static int finishedMatch(TlsConnection *c, Finished *f);
static void tlsConnectionFree(TlsConnection *c);

static int setAlgs(TlsConnection *c, int a);
static int okCipher(Ints *cv);
static int okCompression(Bytes *cv);
static int initCiphers(void);
static Ints* makeciphers(void);

static TlsSec* tlsSecInits(int cvers, uchar *csid, int ncsid, uchar *crandom, uchar *ssid, int *nssid, uchar *srandom);
static int	tlsSecSecrets(TlsSec *sec, int vers, uchar *epm, int nepm, uchar *kd, int nkd);
static TlsSec*	tlsSecInitc(int cvers, uchar *crandom);
static int	tlsSecSecretc(TlsSec *sec, uchar *sid, int nsid, uchar *srandom, uchar *cert, int ncert, int vers, uchar **epm, int *nepm, uchar *kd, int nkd);
static int	tlsSecFinished(TlsSec *sec, HandHash hs, uchar *fin, int nfin, int isclient);
static void	tlsSecOk(TlsSec *sec);
static void	tlsSecKill(TlsSec *sec);
static void	tlsSecClose(TlsSec *sec);
static void	setMasterSecret(TlsSec *sec, Bytes *pm);
static void	serverMasterSecret(TlsSec *sec, uchar *epm, int nepm);
static void	setSecrets(TlsSec *sec, uchar *kd, int nkd);
static int	clientMasterSecret(TlsSec *sec, RSApub *pub, uchar **epm, int *nepm);
static Bytes *pkcs1_encrypt(Bytes* data, RSApub* key, int blocktype);
static Bytes *pkcs1_decrypt(TlsSec *sec, uchar *epm, int nepm);
static void	tlsSetFinished(TlsSec *sec, HandHash hs, uchar *finished, int isClient);
static void	tls12SetFinished(TlsSec *sec, HandHash hs, uchar *finished, int isClient);
static void	sslSetFinished(TlsSec *sec, HandHash hs, uchar *finished, int isClient);
static void	sslPRF(uchar *buf, int nbuf, uchar *key, int nkey, char *label,
			uchar *seed0, int nseed0, uchar *seed1, int nseed1);
static int setVers(TlsSec *sec, int version);

static AuthRpc* factotum_rsa_open(uchar *cert, int certlen);
static mpint* factotum_rsa_decrypt(AuthRpc *rpc, mpint *cipher);
static void factotum_rsa_close(AuthRpc*rpc);

static void	tls12SetFinished384(TlsSec *sec, HandHash hs, uchar *finished, int isClient);
static void	tlsPsha2_384(uchar *buf, int nbuf, uchar *key, int nkey, uchar *label, int nlabel, uchar *seed, int nseed);
static void	tls12PRF384(uchar *buf, int nbuf, uchar *key, int nkey, char *label,
			uchar *seed0, int nseed0, uchar *seed1, int nseed1);

static int	isECDHE(int tlsid);
static int	isECDSA(int tlsid);
static int	isDHE(int tlsid);
static int	tlsSecECDHEs0(TlsSec *sec, Ints *curves);
static Bytes*	tlsSecECDHEs1(TlsSec *sec, int *curve);
static int	tlsSecECDHEs2(TlsSec *sec, Bytes *Yc);
static Bytes*	tlsSecECDHEc(TlsSec *sec, int curve, Bytes *par);
static Bytes*	tlsSecDHEs1(TlsSec *sec, int *curve);
static int	tlsSecDHEs2(TlsSec *sec, Bytes *Yc);
static Bytes*	tlsSecDHEc(TlsSec *sec, Bytes *par);
static void	dhParamsDigest(TlsSec *sec, Bytes *par, uchar *digest);
static Bytes*	pkcs1_sign(TlsSec *sec, uchar *digest, int digestlen);
static int	pkcs1_verify(RSApub *pk, Bytes *sig, uchar *digest, int digestlen);

static void* emalloc(int);
static void* erealloc(void*, int);
static void put32(uchar *p, u32int);
static void put24(uchar *p, int);
static void put16(uchar *p, int);
static u32int get32(uchar *p);
static int get24(uchar *p);
static int get16(uchar *p);
static Bytes* newbytes(int len);
static Bytes* makebytes(uchar* buf, int len);
static void freebytes(Bytes* b);
static Ints* newints(int len);
static Ints* makeints(int* buf, int len);
static void freeints(Ints* b);

//================= client/server ========================

//	push TLS onto fd, returning new (application) file descriptor
//		or -1 if error.
int
tlsServer(int fd, TLSconn *conn)
{
	char buf[8];
	char dname[64];
	int n, data, ctl, hand;
	TlsConnection *tls;

	if(conn == nil)
		return -1;
	ctl = open("#a/tls/clone", ORDWR);
	if(ctl < 0)
		return -1;
	n = read(ctl, buf, sizeof(buf)-1);
	if(n < 0){
		close(ctl);
		return -1;
	}
	buf[n] = 0;
	snprint(conn->dir, sizeof conn->dir, "#a/tls/%s", buf);
	snprint(dname, sizeof dname, "#a/tls/%s/hand", buf);
	hand = open(dname, ORDWR);
	if(hand < 0){
		close(ctl);
		return -1;
	}
	fprint(ctl, "fd %d 0x%x", fd, ProtocolVersion);
	tls = tlsServer2(ctl, hand, conn->cert, conn->certlen, conn->trace, conn->chain);
	snprint(dname, sizeof dname, "#a/tls/%s/data", buf);
	data = open(dname, ORDWR);
	close(fd);
	close(hand);
	close(ctl);
	if(data < 0){
		return -1;
	}
	if(tls == nil){
		close(data);
		return -1;
	}
	if(conn->cert)
		free(conn->cert);
	conn->cert = 0;  // client certificates are not yet implemented
	conn->certlen = 0;
	conn->sessionIDlen = tls->sid->len;
	conn->sessionID = emalloc(conn->sessionIDlen);
	memcpy(conn->sessionID, tls->sid->data, conn->sessionIDlen);
	if(conn->sessionKey != nil && conn->sessionType != nil && strcmp(conn->sessionType, "ttls") == 0)
		tls->sec->prf(conn->sessionKey, conn->sessionKeylen, tls->sec->sec, MasterSecretSize, conn->sessionConst,  tls->sec->crandom, RandomSize, tls->sec->srandom, RandomSize);
	tlsConnectionFree(tls);
	return data;
}

//	push TLS onto fd, returning new (application) file descriptor
//		or -1 if error.
int
tlsClient(int fd, TLSconn *conn)
{
	char buf[8];
	char dname[64];
	int n, data, ctl, hand;
	TlsConnection *tls;

	if(!conn)
		return -1;
	ctl = open("#a/tls/clone", ORDWR);
	if(ctl < 0)
		return -1;
	n = read(ctl, buf, sizeof(buf)-1);
	if(n < 0){
		close(ctl);
		return -1;
	}
	buf[n] = 0;
	snprint(conn->dir, sizeof conn->dir, "#a/tls/%s", buf);
	snprint(dname, sizeof dname, "#a/tls/%s/hand", buf);
	hand = open(dname, ORDWR);
	if(hand < 0){
		close(ctl);
		return -1;
	}
	snprint(dname, sizeof dname, "#a/tls/%s/data", buf);
	data = open(dname, ORDWR);
	if(data < 0) {
		close(hand);
		close(ctl);
		return -1;
	}
	fprint(ctl, "fd %d 0x%x", fd, ProtocolVersion);
	tls = tlsClient2(ctl, hand, conn->sessionID, conn->sessionIDlen, conn->serverName, conn->trace);
	close(fd);
	close(hand);
	close(ctl);
	if(tls == nil){
		close(data);
		return -1;
	}
	conn->certlen = tls->cert->len;
	conn->cert = emalloc(conn->certlen);
	memcpy(conn->cert, tls->cert->data, conn->certlen);
	conn->sessionIDlen = tls->sid->len;
	conn->sessionID = emalloc(conn->sessionIDlen);
	memcpy(conn->sessionID, tls->sid->data, conn->sessionIDlen);
	if(conn->sessionKey != nil && conn->sessionType != nil && strcmp(conn->sessionType, "ttls") == 0)
		tls->sec->prf(conn->sessionKey, conn->sessionKeylen, tls->sec->sec, MasterSecretSize, conn->sessionConst,  tls->sec->crandom, RandomSize, tls->sec->srandom, RandomSize);
	tlsConnectionFree(tls);
	return data;
}

static int
countchain(PEMChain *p)
{
	int i = 0;

	while (p) {
		i++;
		p = p->next;
	}
	return i;
}

static TlsConnection *
tlsServer2(int ctl, int hand, uchar *cert, int ncert, int (*trace)(char*fmt, ...), PEMChain *chp)
{
	TlsConnection *c;
	Msg m;
	Bytes *csid;
	uchar sid[SidSize], kd[MaxKeyData];
	char *secrets;
	int cipher, compressor, nsid, rv, numcerts, i, secReneg;

	if(trace)
		trace("tlsServer2\n");
	if(!initCiphers())
		return nil;
	c = emalloc(sizeof(TlsConnection));
	c->ctl = ctl;
	c->hand = hand;
	c->trace = trace;
	c->version = ProtocolVersion;

	memset(&m, 0, sizeof(m));
	if(!msgRecv(c, &m)){
		if(trace)
			trace("initial msgRecv failed\n");
		goto Err;
	}
	if(m.tag != HClientHello) {
		tlsError(c, EUnexpectedMessage, "expected a client hello");
		goto Err;
	}
	c->clientVersion = m.u.clientHello.version;
	if(trace)
		trace("ClientHello version %x\n", c->clientVersion);
	if(setVersion(c, m.u.clientHello.version) < 0) {
		tlsError(c, EIllegalParameter, "incompatible version");
		goto Err;
	}
	for(i = 0; i < m.u.clientHello.ciphers->len; i++)
		if(m.u.clientHello.ciphers->data[i] == TLS_FALLBACK_SCSV
		&& c->clientVersion < ProtocolVersion){
			tlsError(c, EInappropriateFallback, "inappropriate fallback");
			goto Err;
		}

	memmove(c->crandom, m.u.clientHello.random, RandomSize);
	cipher = okCipher(m.u.clientHello.ciphers);
	if(cipher < 0) {
		// reply with EInsufficientSecurity if we know that's the case
		if(cipher == -2)
			tlsError(c, EInsufficientSecurity, "cipher suites too weak");
		else
			tlsError(c, EHandshakeFailure, "no matching cipher suite");
		goto Err;
	}
	if(!setAlgs(c, cipher)){
		tlsError(c, EHandshakeFailure, "no matching cipher suite");
		goto Err;
	}
	compressor = okCompression(m.u.clientHello.compressors);
	if(compressor < 0) {
		tlsError(c, EHandshakeFailure, "no matching compressor");
		goto Err;
	}

	csid = m.u.clientHello.sid;
	if(trace)
		trace("  cipher %d, compressor %d, csidlen %d\n", cipher, compressor, csid->len);
	c->sec = tlsSecInits(c->clientVersion, csid->data, csid->len, c->crandom, sid, &nsid, c->srandom);
	if(c->sec == nil){
		tlsError(c, EHandshakeFailure, "can't initialize security: %r");
		goto Err;
	}
	c->sec->rpc = factotum_rsa_open(cert, ncert);
	if(c->sec->rpc == nil){
		tlsError(c, EHandshakeFailure, "factotum_rsa_open: %r");
		goto Err;
	}
	c->sec->rsapub = X509toRSApub(cert, ncert, nil, 0);
	c->sec->sha384 = c->sha384;
	if(isECDHE(cipher) && tlsSecECDHEs0(c->sec, m.u.clientHello.curves) < 0){
		tlsError(c, EHandshakeFailure, "no matching elliptic curve");
		goto Err;
	}
	secReneg = m.u.clientHello.secReneg;
	for(i = 0; i < m.u.clientHello.ciphers->len; i++)
		if(m.u.clientHello.ciphers->data[i] == TLS_EMPTY_RENEGOTIATION_INFO_SCSV)
			secReneg = 1;
	msgClear(&m);

	m.tag = HServerHello;
	m.u.serverHello.version = c->version;
	memmove(m.u.serverHello.random, c->srandom, RandomSize);
	m.u.serverHello.cipher = cipher;
	m.u.serverHello.compressor = compressor;
	m.u.serverHello.secReneg = secReneg;
	c->sid = makebytes(sid, nsid);
	m.u.serverHello.sid = makebytes(c->sid->data, c->sid->len);
	if(!msgSend(c, &m, AQueue))
		goto Err;
	msgClear(&m);

	m.tag = HCertificate;
	numcerts = countchain(chp);
	m.u.certificate.ncert = 1 + numcerts;
	m.u.certificate.certs = emalloc(m.u.certificate.ncert * sizeof(Bytes));
	m.u.certificate.certs[0] = makebytes(cert, ncert);
	for (i = 0; i < numcerts && chp; i++, chp = chp->next)
		m.u.certificate.certs[i+1] = makebytes(chp->pem, chp->pemlen);
	if(!msgSend(c, &m, AQueue))
		goto Err;
	msgClear(&m);

	if(isECDHE(cipher) || isDHE(cipher)){
		uchar digest[SHA2_256dlen];

		m.tag = HServerKeyExchange;
		if(isECDHE(cipher))
			m.u.serverKeyExchange.key = tlsSecECDHEs1(c->sec, &m.u.serverKeyExchange.curve);
		else
			m.u.serverKeyExchange.key = tlsSecDHEs1(c->sec, &m.u.serverKeyExchange.curve);
		if(m.u.serverKeyExchange.key == nil){
			tlsError(c, EHandshakeFailure, "can't make server key exchange: %r");
			goto Err;
		}
		m.u.serverKeyExchange.sigalg = RSA_PKCS1_SHA256;
		dhParamsDigest(c->sec, m.u.serverKeyExchange.key, digest);
		m.u.serverKeyExchange.signature = pkcs1_sign(c->sec, digest, SHA2_256dlen);
		if(m.u.serverKeyExchange.signature == nil){
			tlsError(c, EHandshakeFailure, "can't sign server key exchange: %r");
			goto Err;
		}
		if(!msgSend(c, &m, AQueue))
			goto Err;
		msgClear(&m);
	}

	m.tag = HServerHelloDone;
	if(!msgSend(c, &m, AFlush))
		goto Err;
	msgClear(&m);

	if(!msgRecv(c, &m))
		goto Err;
	if(m.tag != HClientKeyExchange) {
		tlsError(c, EUnexpectedMessage, "expected a client key exchange");
		goto Err;
	}
	if(isECDHE(cipher) || isDHE(cipher)){
		if(setVers(c->sec, c->version) < 0
		|| (isECDHE(cipher) ? tlsSecECDHEs2(c->sec, m.u.clientKeyExchange.key)
				: tlsSecDHEs2(c->sec, m.u.clientKeyExchange.key)) < 0){
			tlsError(c, EHandshakeFailure, "couldn't set secrets: %r");
			goto Err;
		}
		setSecrets(c->sec, kd, c->nsecret);
	}else if(tlsSecSecrets(c->sec, c->version, m.u.clientKeyExchange.key->data, m.u.clientKeyExchange.key->len, kd, c->nsecret) < 0){
		tlsError(c, EHandshakeFailure, "couldn't set secrets: %r");
		goto Err;
	}
	if(trace)
		trace("tls secrets\n");
	secrets = (char*)emalloc(2*c->nsecret);
	enc64(secrets, 2*c->nsecret, kd, c->nsecret);
	rv = fprint(c->ctl, "secret %s %s 0 %s", c->digest, c->enc, secrets);
	memset(secrets, 0, 2*c->nsecret);
	free(secrets);
	memset(kd, 0, c->nsecret);
	if(rv < 0){
		tlsError(c, EHandshakeFailure, "can't set keys: %r");
		goto Err;
	}
	msgClear(&m);

	/* no CertificateVerify; skip to Finished */
	if(tlsSecFinished(c->sec, c->hs, c->finished.verify, c->finished.n, 1) < 0){
		tlsError(c, EInternalError, "can't set finished: %r");
		goto Err;
	}
	if(!msgRecv(c, &m))
		goto Err;
	if(m.tag != HFinished) {
		tlsError(c, EUnexpectedMessage, "expected a finished");
		goto Err;
	}
	if(!finishedMatch(c, &m.u.finished)) {
		tlsError(c, EHandshakeFailure, "finished verification failed");
		goto Err;
	}
	msgClear(&m);

	/* change cipher spec */
	if(fprint(c->ctl, "changecipher") < 0){
		tlsError(c, EInternalError, "can't enable cipher: %r");
		goto Err;
	}

	if(tlsSecFinished(c->sec, c->hs, c->finished.verify, c->finished.n, 0) < 0){
		tlsError(c, EInternalError, "can't set finished: %r");
		goto Err;
	}
	m.tag = HFinished;
	m.u.finished = c->finished;
	if(!msgSend(c, &m, AFlush))
		goto Err;
	msgClear(&m);
	if(trace)
		trace("tls finished\n");

	if(fprint(c->ctl, "opened") < 0)
		goto Err;
	tlsSecOk(c->sec);
	return c;

Err:
	msgClear(&m);
	tlsConnectionFree(c);
	return 0;
}

static TlsConnection *
tlsClient2(int ctl, int hand, uchar *csid, int ncsid, char *serverName, int (*trace)(char*fmt, ...))
{
	TlsConnection *c;
	Msg m;
	uchar kd[MaxKeyData], *epm;
	char *secrets;
	int creq, nepm, rv;

	if(!initCiphers())
		return nil;
	epm = nil;
	c = emalloc(sizeof(TlsConnection));
	c->version = ProtocolVersion;
	c->ctl = ctl;
	c->hand = hand;
	c->trace = trace;
	c->isClient = 1;
	c->clientVersion = c->version;

	c->sec = tlsSecInitc(c->clientVersion, c->crandom);
	if(c->sec == nil)
		goto Err;

	/* client hello */
	memset(&m, 0, sizeof(m));
	m.tag = HClientHello;
	m.u.clientHello.version = c->clientVersion;
	memmove(m.u.clientHello.random, c->crandom, RandomSize);
	m.u.clientHello.sid = makebytes(csid, ncsid);
	m.u.clientHello.ciphers = makeciphers();
	m.u.clientHello.compressors = makebytes(compressors,sizeof(compressors));
	m.u.clientHello.serverName = serverName;
	if(c->clientVersion >= TLS12Version){
		m.u.clientHello.sigAlgs = makeints(sigAlgs, nelem(sigAlgs));
		m.u.clientHello.curves = makeints(tlscurves, nelem(tlscurves));
	}
	if(!msgSend(c, &m, AFlush))
		goto Err;
	msgClear(&m);

	/* server hello */
	if(!msgRecv(c, &m))
		goto Err;
	if(m.tag != HServerHello) {
		tlsError(c, EUnexpectedMessage, "expected a server hello");
		goto Err;
	}
	if(setVersion(c, m.u.serverHello.version) < 0) {
		tlsError(c, EIllegalParameter, "incompatible version %r");
		goto Err;
	}
	memmove(c->srandom, m.u.serverHello.random, RandomSize);
	c->sid = makebytes(m.u.serverHello.sid->data, m.u.serverHello.sid->len);
	if(c->sid->len != 0 && c->sid->len != SidSize) {
		tlsError(c, EIllegalParameter, "invalid server session identifier");
		goto Err;
	}
	if(!setAlgs(c, m.u.serverHello.cipher)) {
		tlsError(c, EIllegalParameter, "invalid cipher suite");
		goto Err;
	}
	c->sec->sha384 = c->sha384;
	if(m.u.serverHello.compressor != CompressionNull) {
		tlsError(c, EIllegalParameter, "invalid compression");
		goto Err;
	}
	msgClear(&m);

	/* certificate */
	if(!msgRecv(c, &m) || m.tag != HCertificate) {
		tlsError(c, EUnexpectedMessage, "expected a certificate");
		goto Err;
	}
	if(m.u.certificate.ncert < 1) {
		tlsError(c, EIllegalParameter, "runt certificate");
		goto Err;
	}
	c->cert = makebytes(m.u.certificate.certs[0]->data, m.u.certificate.certs[0]->len);
	msgClear(&m);

	/* server key exchange (optional) */
	if(!msgRecv(c, &m))
		goto Err;
	if(m.tag == HServerKeyExchange) {
		uchar digest[SHA2_256dlen];
		Bytes *par;
		RSApub *pub;

		if(!isECDHE(c->cipher) && !isDHE(c->cipher)){
			tlsError(c, EUnexpectedMessage, "unexpected server key exchange");
			goto Err;
		}
		par = m.u.serverKeyExchange.key;
		memmove(c->sec->srandom, c->srandom, RandomSize);
		dhParamsDigest(c->sec, par, digest);
		if((m.u.serverKeyExchange.sigalg & 0xff) == 0x03){
			ECdomain dom;
			ECpub *ecpub;
			char *e;

			ecpub = X509toECpub(c->cert->data, c->cert->len, nil, 0, &dom);
			if(ecpub == nil){
				tlsError(c, EBadCertificate, "invalid x509/ecdsa certificate");
				goto Err;
			}
			e = X509ecdsaverifydigest(m.u.serverKeyExchange.signature->data,
				m.u.serverKeyExchange.signature->len, digest, SHA2_256dlen, &dom, ecpub);
			ecdomfree(&dom);
			ecpubfree(ecpub);
			if(e != nil){
				tlsError(c, EDecryptError, "can't verify ecdsa server key exchange");
				goto Err;
			}
		}else if(m.u.serverKeyExchange.sigalg == RSA_PKCS1_SHA256){
			pub = X509toRSApub(c->cert->data, c->cert->len, nil, 0);
			if(pub == nil){
				tlsError(c, EBadCertificate, "invalid x509/rsa certificate");
				goto Err;
			}
			if(pkcs1_verify(pub, m.u.serverKeyExchange.signature, digest, SHA2_256dlen) < 0){
				rsapubfree(pub);
				tlsError(c, EDecryptError, "can't verify server key exchange signature");
				goto Err;
			}
			rsapubfree(pub);
		}else{
			tlsError(c, EHandshakeFailure, "unsupported server key exchange signature");
			goto Err;
		}
		c->curve = m.u.serverKeyExchange.curve;
		c->Ys = m.u.serverKeyExchange.key;
		m.u.serverKeyExchange.key = nil;
		msgClear(&m);
		if(!msgRecv(c, &m))
			goto Err;
	}

	/* certificate request (optional) */
	creq = 0;
	if(m.tag == HCertificateRequest) {
		creq = 1;
		msgClear(&m);
		if(!msgRecv(c, &m))
			goto Err;
	}

	if(m.tag != HServerHelloDone) {
		tlsError(c, EUnexpectedMessage, "expected a server hello done");
		goto Err;
	}
	msgClear(&m);

	if(isECDHE(c->cipher) || isDHE(c->cipher)){
		Bytes *Yc;

		if(setVers(c->sec, c->version) < 0){
			tlsError(c, EHandshakeFailure, "can't set version: %r");
			goto Err;
		}
		if(isECDHE(c->cipher))
			Yc = tlsSecECDHEc(c->sec, c->curve, c->Ys);
		else
			Yc = tlsSecDHEc(c->sec, c->Ys);
		if(Yc == nil){
			tlsError(c, EHandshakeFailure, "can't make key exchange: %r");
			goto Err;
		}
		setSecrets(c->sec, kd, c->nsecret);
		nepm = Yc->len;
		epm = malloc(nepm);
		if(epm != nil)
			memmove(epm, Yc->data, nepm);
		freebytes(Yc);
	}else if(tlsSecSecretc(c->sec, c->sid->data, c->sid->len, c->srandom,
			c->cert->data, c->cert->len, c->version, &epm, &nepm,
			kd, c->nsecret) < 0){
		tlsError(c, EBadCertificate, "invalid x509/rsa certificate");
		goto Err;
	}
	secrets = (char*)emalloc(2*c->nsecret);
	enc64(secrets, 2*c->nsecret, kd, c->nsecret);
	rv = fprint(c->ctl, "secret %s %s 1 %s", c->digest, c->enc, secrets);
	memset(secrets, 0, 2*c->nsecret);
	free(secrets);
	memset(kd, 0, c->nsecret);
	if(rv < 0){
		tlsError(c, EHandshakeFailure, "can't set keys: %r");
		goto Err;
	}

	if(creq) {
		/* send a zero length certificate */
		m.tag = HCertificate;
		if(!msgSend(c, &m, AFlush))
			goto Err;
		msgClear(&m);
	}

	/* client key exchange */
	m.tag = HClientKeyExchange;
	m.u.clientKeyExchange.key = makebytes(epm, nepm);
	free(epm);
	epm = nil;
	if(m.u.clientKeyExchange.key == nil) {
		tlsError(c, EHandshakeFailure, "can't set secret: %r");
		goto Err;
	}
	if(!msgSend(c, &m, AFlush))
		goto Err;
	msgClear(&m);

	/* change cipher spec */
	if(fprint(c->ctl, "changecipher") < 0){
		tlsError(c, EInternalError, "can't enable cipher: %r");
		goto Err;
	}

	// Cipherchange must occur immediately before Finished to avoid
	// potential hole;  see section 4.3 of Wagner Schneier 1996.
	if(tlsSecFinished(c->sec, c->hs, c->finished.verify, c->finished.n, 1) < 0){
		tlsError(c, EInternalError, "can't set finished 1: %r");
		goto Err;
	}
	m.tag = HFinished;
	m.u.finished = c->finished;

	if(!msgSend(c, &m, AFlush)) {
		tlsError(c, EInternalError, "can't flush after client Finished: %r");
		goto Err;
	}
	msgClear(&m);

	if(tlsSecFinished(c->sec, c->hs, c->finished.verify, c->finished.n, 0) < 0){
		tlsError(c, EInternalError, "can't set finished 0: %r");
		goto Err;
	}
	if(!msgRecv(c, &m)) {
		tlsError(c, EInternalError, "can't read server Finished: %r");
		goto Err;
	}
	if(m.tag != HFinished) {
		tlsError(c, EUnexpectedMessage, "expected a Finished msg from server");
		goto Err;
	}

	if(!finishedMatch(c, &m.u.finished)) {
		tlsError(c, EHandshakeFailure, "finished verification failed");
		goto Err;
	}
	msgClear(&m);

	if(fprint(c->ctl, "opened") < 0){
		if(trace)
			trace("unable to do final open: %r\n");
		goto Err;
	}
	tlsSecOk(c->sec);
	return c;

Err:
	free(epm);
	msgClear(&m);
	tlsConnectionFree(c);
	return 0;
}


//================= message functions ========================

static uchar sendbuf[9000], *sendp;

static void
msgHash(TlsConnection *c, uchar *p, int n)
{
	md5(p, n, 0, &c->hs.md5);
	sha1(p, n, 0, &c->hs.sha1);
	if(c->version >= TLS12Version){
		sha2_256(p, n, 0, &c->hs.sha2_256);
		sha2_384(p, n, 0, &c->hs.sha2_384);
	}else{
		memset(&c->hs.sha2_256, 0, sizeof c->hs.sha2_256);
		memset(&c->hs.sha2_384, 0, sizeof c->hs.sha2_384);
	}
}

static int
msgSend(TlsConnection *c, Msg *m, int act)
{
	uchar *p, *ep, *q; // sendp = start of new message;  p = write pointer
	int nn, n, i;

	if(sendp == nil)
		sendp = sendbuf;
	p = sendp;
	if(c->trace)
		c->trace("send %s", msgPrint((char*)p, (sizeof sendbuf) - (p-sendbuf), m));

	p[0] = m->tag;	// header - fill in size later
	p += 4;

	switch(m->tag) {
	default:
		tlsError(c, EInternalError, "can't encode a %d", m->tag);
		goto Err;
	case HClientHello:
		// version
		put16(p, m->u.clientHello.version);
		p += 2;

		// random
		memmove(p, m->u.clientHello.random, RandomSize);
		p += RandomSize;

		// sid
		n = m->u.clientHello.sid->len;
		assert(n < 256);
		p[0] = n;
		memmove(p+1, m->u.clientHello.sid->data, n);
		p += n+1;

		n = m->u.clientHello.ciphers->len;
		assert(n > 0 && n < 200);
		put16(p, n*2);
		p += 2;
		for(i=0; i<n; i++) {
			put16(p, m->u.clientHello.ciphers->data[i]);
			p += 2;
		}

		n = m->u.clientHello.compressors->len;
		assert(n > 0);
		p[0] = n;
		memmove(p+1, m->u.clientHello.compressors->data, n);
		p += n+1;

		ep = p + 2;
		q = ep;

		if(m->u.clientHello.serverName != nil) {
			n = strlen(m->u.clientHello.serverName);
			put16(q, ExtServerName);
			put16(q+2, 2+1+2+n); /* length of extension content */
			put16(q+4, 1+2+n);   /* length of server name list */
			q[6] = 0;            /* host name type */
			put16(q+7, n);       /* length of host name */
			memmove(q+9, m->u.clientHello.serverName, n);
			q += 9 + n;
		}

		if(m->u.clientHello.sigAlgs != nil) {
			n = m->u.clientHello.sigAlgs->len;
			put16(q, ExtSigalgs);
			put16(q+2, 2 + 2*n); /* length of extension content */
			put16(q+4, 2*n);     /* length of algorithm list */
			q += 6;
			for(i = 0; i < n; i++) {
				put16(q, m->u.clientHello.sigAlgs->data[i]);
				q += 2;
			}
		}

		if(m->u.clientHello.curves != nil) {
			n = m->u.clientHello.curves->len;
			put16(q, ExtEllipticCurves);
			put16(q+2, 2 + 2*n); /* length of extension content */
			put16(q+4, 2*n);     /* length of curve list */
			q += 6;
			for(i = 0; i < n; i++) {
				put16(q, m->u.clientHello.curves->data[i]);
				q += 2;
			}
			put16(q, ExtPointFormats);
			put16(q+2, 2);       /* length of extension content */
			q[4] = 1;            /* length of point format list */
			q[5] = 0;            /* uncompressed */
			q += 6;
		}

		if(q > ep) {
			put16(p, q - ep); /* length of extensions */
			p = q;
		}
		break;
	case HServerHello:
		put16(p, m->u.serverHello.version);
		p += 2;

		// random
		memmove(p, m->u.serverHello.random, RandomSize);
		p += RandomSize;

		// sid
		n = m->u.serverHello.sid->len;
		assert(n < 256);
		p[0] = n;
		memmove(p+1, m->u.serverHello.sid->data, n);
		p += n+1;

		put16(p, m->u.serverHello.cipher);
		p += 2;
		p[0] = m->u.serverHello.compressor;
		p += 1;
		if(m->u.serverHello.secReneg){
			if(p + 7 - sendbuf > sizeof(sendbuf)){
				tlsError(c, EInternalError, "output buffer too small for server hello");
				goto Err;
			}
			put16(p, 5);
			put16(p+2, ExtRenegInfo);
			put16(p+4, 1);
			p[6] = 0;
			p += 7;
		}
		break;
	case HServerHelloDone:
		break;
	case HServerKeyExchange:
		n = m->u.serverKeyExchange.key->len;
		memmove(p, m->u.serverKeyExchange.key->data, n);
		p += n;
		put16(p, m->u.serverKeyExchange.sigalg);
		p += 2;
		n = m->u.serverKeyExchange.signature->len;
		put16(p, n);
		p += 2;
		memmove(p, m->u.serverKeyExchange.signature->data, n);
		p += n;
		break;
	case HCertificate:
		nn = 0;
		for(i = 0; i < m->u.certificate.ncert; i++)
			nn += 3 + m->u.certificate.certs[i]->len;
		if(p + 3 + nn - sendbuf > sizeof(sendbuf)) {
			tlsError(c, EInternalError, "output buffer too small for certificate");
			goto Err;
		}
		put24(p, nn);
		p += 3;
		for(i = 0; i < m->u.certificate.ncert; i++){
			put24(p, m->u.certificate.certs[i]->len);
			p += 3;
			memmove(p, m->u.certificate.certs[i]->data, m->u.certificate.certs[i]->len);
			p += m->u.certificate.certs[i]->len;
		}
		break;
	case HClientKeyExchange:
		n = m->u.clientKeyExchange.key->len;
		if(isECDHE(c->cipher)){
			/* the ec public key has a one-byte length prefix */
			assert(n < 256);
			p[0] = n;
			p += 1;
		}else if(c->version != SSL3Version){
			put16(p, n);
			p += 2;
		}
		memmove(p, m->u.clientKeyExchange.key->data, n);
		p += n;
		break;
	case HFinished:
		memmove(p, m->u.finished.verify, m->u.finished.n);
		p += m->u.finished.n;
		break;
	}

	// go back and fill in size
	n = p-sendp;
	assert(p <= sendbuf+sizeof(sendbuf));
	put24(sendp+1, n-4);

	// remember hash of Handshake messages
	if(m->tag != HHelloRequest) {
		msgHash(c, sendp, n);
	}

	sendp = p;
	if(act == AFlush){
		sendp = sendbuf;
		if(write(c->hand, sendbuf, p-sendbuf) < 0){
			if(c->trace)
				c->trace("write error: %r\n");
			goto Err;
		}
	}
	msgClear(m);
	return 1;
Err:
	msgClear(m);
	return 0;
}

static uchar*
tlsReadN(TlsConnection *c, int n)
{
	uchar *p;
	int nn, nr;

	nn = c->ep - c->rp;
	if(nn < n){
		if(c->rp != c->buf){
			memmove(c->buf, c->rp, nn);
			c->rp = c->buf;
			c->ep = &c->buf[nn];
		}
		for(; nn < n; nn += nr) {
			nr = read(c->hand, &c->rp[nn], n - nn);
			if(nr <= 0)
				return nil;
			c->ep += nr;
		}
	}
	p = c->rp;
	c->rp += n;
	return p;
}

static int
msgRecv(TlsConnection *c, Msg *m)
{
	uchar *p;
	int type, n, nn, nx, i, nsid, nrandom, nciph;

	for(;;) {
		p = tlsReadN(c, 4);
		if(p == nil)
			return 0;
		type = p[0];
		n = get24(p+1);

		if(type != HHelloRequest)
			break;
		if(n != 0) {
			tlsError(c, EDecodeError, "invalid hello request during handshake");
			return 0;
		}
	}

	if(n > sizeof(c->buf)) {
		tlsError(c, EDecodeError, "handshake message too long %d %d", n, sizeof(c->buf));
		return 0;
	}

	if(type == HSSL2ClientHello){
		/* Cope with an SSL3 ClientHello expressed in SSL2 record format.
			This is sent by some clients that we must interoperate
			with, such as Java's JSSE and Microsoft's Internet Explorer. */
		p = tlsReadN(c, n);
		if(p == nil)
			return 0;
		msgHash(c, p, n);
		m->tag = HClientHello;
		if(n < 22)
			goto Short;
		m->u.clientHello.version = get16(p+1);
		p += 3;
		n -= 3;
		nn = get16(p); /* cipher_spec_len */
		nsid = get16(p + 2);
		nrandom = get16(p + 4);
		p += 6;
		n -= 6;
		if(nsid != 0 	/* no sid's, since shouldn't restart using ssl2 header */
				|| nrandom < 16 || nn % 3 || n - nrandom < nn)
			goto Err;
		if(c->trace && (n - nrandom != nn))
			c->trace("n-nrandom!=nn: n=%d nrandom=%d nn=%d\n", n, nrandom, nn);
		/* ignore ssl2 ciphers and look for {0x00, ssl3 cipher} */
		nciph = 0;
		for(i = 0; i < nn; i += 3)
			if(p[i] == 0)
				nciph++;
		m->u.clientHello.ciphers = newints(nciph);
		nciph = 0;
		for(i = 0; i < nn; i += 3)
			if(p[i] == 0)
				m->u.clientHello.ciphers->data[nciph++] = get16(&p[i + 1]);
		p += nn;
		m->u.clientHello.sid = makebytes(nil, 0);
		if(nrandom > RandomSize)
			nrandom = RandomSize;
		memset(m->u.clientHello.random, 0, RandomSize - nrandom);
		memmove(&m->u.clientHello.random[RandomSize - nrandom], p, nrandom);
		m->u.clientHello.compressors = newbytes(1);
		m->u.clientHello.compressors->data[0] = CompressionNull;
		goto Ok;
	}

	msgHash(c, p, 4);

	p = tlsReadN(c, n);
	if(p == nil)
		return 0;

	msgHash(c, p, n);

	m->tag = type;

	switch(type) {
	default:
		tlsError(c, EUnexpectedMessage, "can't decode a %d", type);
		goto Err;
	case HClientHello:
		if(n < 2)
			goto Short;
		m->u.clientHello.version = get16(p);
		p += 2;
		n -= 2;

		if(n < RandomSize)
			goto Short;
		memmove(m->u.clientHello.random, p, RandomSize);
		p += RandomSize;
		n -= RandomSize;
		if(n < 1 || n < p[0]+1)
			goto Short;
		m->u.clientHello.sid = makebytes(p+1, p[0]);
		p += m->u.clientHello.sid->len+1;
		n -= m->u.clientHello.sid->len+1;

		if(n < 2)
			goto Short;
		nn = get16(p);
		p += 2;
		n -= 2;

		if((nn & 1) || n < nn || nn < 2)
			goto Short;
		m->u.clientHello.ciphers = newints(nn >> 1);
		for(i = 0; i < nn; i += 2)
			m->u.clientHello.ciphers->data[i >> 1] = get16(&p[i]);
		p += nn;
		n -= nn;

		if(n < 1 || n < p[0]+1 || p[0] == 0)
			goto Short;
		nn = p[0];
		m->u.clientHello.compressors = newbytes(nn);
		memmove(m->u.clientHello.compressors->data, p+1, nn);
		p += nn + 1;
		n -= nn + 1;

		/* extensions */
		if(n == 0)
			break;
		if(n < 2)
			goto Short;
		nx = get16(p);
		p += 2;
		n -= 2;
		while(nx > 0){
			if(n < nx || nx < 4)
				goto Short;
			i = get16(p);
			nn = get16(p+2);
			if(nx < nn+4)
				goto Short;
			nx -= nn+4;
			p += 4;
			n -= 4;
			if(i == ExtSigalgs){
				if(get16(p) != nn-2)
					goto Short;
				p += 2;
				n -= 2;
				nn -= 2;
				m->u.clientHello.sigAlgs = newints(nn/2);
				for(i = 0; i < nn; i += 2)
					m->u.clientHello.sigAlgs->data[i >> 1] = get16(&p[i]);
			} else if(i == ExtEllipticCurves){
				int j, nc;
				if(nn < 2 || get16(p) != nn-2 || ((nn-2) & 1))
					goto Short;
				nc = (nn-2)/2;
				m->u.clientHello.curves = newints(nc);
				for(j = 0; j < nc; j++)
					m->u.clientHello.curves->data[j] = get16(p+2+2*j);
			} else if(i == ExtRenegInfo){
				if(nn < 1 || p[0] != nn-1)
					goto Short;
				if(p[0] != 0){
					tlsError(c, EHandshakeFailure, "invalid renegotiation extension");
					goto Err;
				}
				m->u.clientHello.secReneg = 1;
			}
			p += nn;
			n -= nn;
		}
		break;
	case HServerHello:
		if(n < 2)
			goto Short;
		m->u.serverHello.version = get16(p);
		p += 2;
		n -= 2;

		if(n < RandomSize)
			goto Short;
		memmove(m->u.serverHello.random, p, RandomSize);
		p += RandomSize;
		n -= RandomSize;

		if(n < 1 || n < p[0]+1)
			goto Short;
		m->u.serverHello.sid = makebytes(p+1, p[0]);
		p += m->u.serverHello.sid->len+1;
		n -= m->u.serverHello.sid->len+1;

		if(n < 3)
			goto Short;
		m->u.serverHello.cipher = get16(p);
		m->u.serverHello.compressor = p[2];
		n = 0;	/* skip extensions */
		break;
	case HCertificate:
		if(n < 3)
			goto Short;
		nn = get24(p);
		p += 3;
		n -= 3;
		if(n != nn)
			goto Short;
		/* certs */
		i = 0;
		while(n > 0) {
			if(n < 3)
				goto Short;
			nn = get24(p);
			p += 3;
			n -= 3;
			if(nn > n)
				goto Short;
			m->u.certificate.ncert = i+1;
			m->u.certificate.certs = erealloc(m->u.certificate.certs, (i+1)*sizeof(Bytes));
			m->u.certificate.certs[i] = makebytes(p, nn);
			p += nn;
			n -= nn;
			i++;
		}
		break;
	case HServerKeyExchange:
		if(isECDHE(c->cipher)){
			/* named curve ECDHE parameters */
			if(n < 4 || p[0] != 3)
				goto Short;
			m->u.serverKeyExchange.curve = get16(p+1);
			nn = p[3];
			if(n < 4+nn)
				goto Short;
			/* keep the raw params for the signature */
			m->u.serverKeyExchange.key = makebytes(p, 4+nn);
			p += 4+nn;
			n -= 4+nn;
		}else if(isDHE(c->cipher)){
			/* dh_p, dh_g, dh_Ys, each 2-byte length prefixed */
			uchar *q = p;
			int j;

			for(j = 0; j < 3; j++){
				if(n < 2)
					goto Short;
				nn = get16(p);
				p += 2;
				n -= 2;
				if(n < nn)
					goto Short;
				p += nn;
				n -= nn;
			}
			m->u.serverKeyExchange.key = makebytes(q, p - q);
		}else
			goto Short;
		if(c->version >= TLS12Version){
			if(n < 2)
				goto Short;
			m->u.serverKeyExchange.sigalg = get16(p);
			p += 2;
			n -= 2;
		}
		if(n < 2)
			goto Short;
		nn = get16(p);
		p += 2;
		n -= 2;
		if(n < nn)
			goto Short;
		m->u.serverKeyExchange.signature = makebytes(p, nn);
		n -= nn;
		break;
	case HCertificateRequest:
		if(n < 1)
			goto Short;
		nn = p[0];
		p += 1;
		n -= 1;
		if(nn < 1 || nn > n)
			goto Short;
		m->u.certificateRequest.types = makebytes(p, nn);
		p += nn;
		n -= nn;
		if(n < 2)
			goto Short;
		nn = get16(p);
		p += 2;
		n -= 2;
		/* nn == 0 can happen; yahoo's servers do it */
		if(nn != n)
			goto Short;
		/* cas */
		i = 0;
		while(n > 0) {
			if(n < 2)
				goto Short;
			nn = get16(p);
			p += 2;
			n -= 2;
			if(nn < 1 || nn > n)
				goto Short;
			m->u.certificateRequest.nca = i+1;
			m->u.certificateRequest.cas = erealloc(
				m->u.certificateRequest.cas, (i+1)*sizeof(Bytes));
			m->u.certificateRequest.cas[i] = makebytes(p, nn);
			p += nn;
			n -= nn;
			i++;
		}
		break;
	case HServerHelloDone:
		break;
	case HClientKeyExchange:
		/*
		 * this message depends upon the encryption selected
		 */
		if(isECDHE(c->cipher)){
			/* the ec public key has a one-byte length prefix */
			if(n < 1)
				goto Short;
			nn = p[0];
			p += 1;
			n -= 1;
		}else if(c->version == SSL3Version)
			nn = n;
		else{
			if(n < 2)
				goto Short;
			nn = get16(p);
			p += 2;
			n -= 2;
		}
		if(n < nn)
			goto Short;
		m->u.clientKeyExchange.key = makebytes(p, nn);
		n -= nn;
		break;
	case HFinished:
		m->u.finished.n = c->finished.n;
		if(n < m->u.finished.n)
			goto Short;
		memmove(m->u.finished.verify, p, m->u.finished.n);
		n -= m->u.finished.n;
		break;
	}

	if(type != HClientHello && n != 0)
		goto Short;
Ok:
	if(c->trace){
		char *buf;
		buf = emalloc(8000);
		c->trace("recv %s", msgPrint(buf, 8000, m));
		free(buf);
	}
	return 1;
Short:
	tlsError(c, EDecodeError, "handshake message has invalid length");
Err:
	msgClear(m);
	return 0;
}

static void
msgClear(Msg *m)
{
	int i;

	switch(m->tag) {
	default:
		sysfatal("msgClear: unknown message type: %d", m->tag);
	case HHelloRequest:
		break;
	case HClientHello:
		freebytes(m->u.clientHello.sid);
		freeints(m->u.clientHello.ciphers);
		m->u.clientHello.ciphers = nil;
		freebytes(m->u.clientHello.compressors);
		freeints(m->u.clientHello.sigAlgs);
		freeints(m->u.clientHello.curves);
		break;
	case HServerHello:
		freebytes(m->u.clientHello.sid);
		break;
	case HServerKeyExchange:
		freebytes(m->u.serverKeyExchange.key);
		freebytes(m->u.serverKeyExchange.signature);
		break;
	case HCertificate:
		for(i=0; i<m->u.certificate.ncert; i++)
			freebytes(m->u.certificate.certs[i]);
		free(m->u.certificate.certs);
		break;
	case HCertificateRequest:
		freebytes(m->u.certificateRequest.types);
		for(i=0; i<m->u.certificateRequest.nca; i++)
			freebytes(m->u.certificateRequest.cas[i]);
		free(m->u.certificateRequest.cas);
		break;
	case HServerHelloDone:
		break;
	case HClientKeyExchange:
		freebytes(m->u.clientKeyExchange.key);
		break;
	case HFinished:
		break;
	}
	memset(m, 0, sizeof(Msg));
}

static char *
bytesPrint(char *bs, char *be, char *s0, Bytes *b, char *s1)
{
	int i;

	if(s0)
		bs = seprint(bs, be, "%s", s0);
	bs = seprint(bs, be, "[");
	if(b == nil)
		bs = seprint(bs, be, "nil");
	else
		for(i=0; i<b->len; i++)
			bs = seprint(bs, be, "%.2x ", b->data[i]);
	bs = seprint(bs, be, "]");
	if(s1)
		bs = seprint(bs, be, "%s", s1);
	return bs;
}

static char *
intsPrint(char *bs, char *be, char *s0, Ints *b, char *s1)
{
	int i;

	if(s0)
		bs = seprint(bs, be, "%s", s0);
	bs = seprint(bs, be, "[");
	if(b == nil)
		bs = seprint(bs, be, "nil");
	else
		for(i=0; i<b->len; i++)
			bs = seprint(bs, be, "%x ", b->data[i]);
	bs = seprint(bs, be, "]");
	if(s1)
		bs = seprint(bs, be, "%s", s1);
	return bs;
}

static char*
msgPrint(char *buf, int n, Msg *m)
{
	int i;
	char *bs = buf, *be = buf+n;

	switch(m->tag) {
	default:
		bs = seprint(bs, be, "unknown %d\n", m->tag);
		break;
	case HClientHello:
		bs = seprint(bs, be, "ClientHello\n");
		bs = seprint(bs, be, "\tversion: %.4x\n", m->u.clientHello.version);
		bs = seprint(bs, be, "\trandom: ");
		for(i=0; i<RandomSize; i++)
			bs = seprint(bs, be, "%.2x", m->u.clientHello.random[i]);
		bs = seprint(bs, be, "\n");
		bs = bytesPrint(bs, be, "\tsid: ", m->u.clientHello.sid, "\n");
		bs = intsPrint(bs, be, "\tciphers: ", m->u.clientHello.ciphers, "\n");
		bs = bytesPrint(bs, be, "\tcompressors: ", m->u.clientHello.compressors, "\n");
		if(m->u.clientHello.sigAlgs != nil)
			bs = intsPrint(bs, be, "\tsigAlgs: ", m->u.clientHello.sigAlgs, "\n");
		if(m->u.clientHello.serverName != nil)
			bs = seprint(bs, be, "\tserverName: %s\n", m->u.clientHello.serverName);
		bs = seprint(bs, be, "\tsecReneg: %d\n", m->u.clientHello.secReneg);
		break;
	case HServerHello:
		bs = seprint(bs, be, "ServerHello\n");
		bs = seprint(bs, be, "\tversion: %.4x\n", m->u.serverHello.version);
		bs = seprint(bs, be, "\trandom: ");
		for(i=0; i<RandomSize; i++)
			bs = seprint(bs, be, "%.2x", m->u.serverHello.random[i]);
		bs = seprint(bs, be, "\n");
		bs = bytesPrint(bs, be, "\tsid: ", m->u.serverHello.sid, "\n");
		bs = seprint(bs, be, "\tcipher: %.4x\n", m->u.serverHello.cipher);
		bs = seprint(bs, be, "\tcompressor: %.2x\n", m->u.serverHello.compressor);
		bs = seprint(bs, be, "\tsecReneg: %d\n", m->u.serverHello.secReneg);
		break;
	case HCertificate:
		bs = seprint(bs, be, "Certificate\n");
		for(i=0; i<m->u.certificate.ncert; i++)
			bs = bytesPrint(bs, be, "\t", m->u.certificate.certs[i], "\n");
		break;
	case HCertificateRequest:
		bs = seprint(bs, be, "CertificateRequest\n");
		bs = bytesPrint(bs, be, "\ttypes: ", m->u.certificateRequest.types, "\n");
		bs = seprint(bs, be, "\tcertificateauthorities\n");
		for(i=0; i<m->u.certificateRequest.nca; i++)
			bs = bytesPrint(bs, be, "\t\t", m->u.certificateRequest.cas[i], "\n");
		break;
	case HServerHelloDone:
		bs = seprint(bs, be, "ServerHelloDone\n");
		break;
	case HServerKeyExchange:
		bs = seprint(bs, be, "HServerKeyExchange\n");
		bs = seprint(bs, be, "\tcurve: %x\n", m->u.serverKeyExchange.curve);
		bs = bytesPrint(bs, be, "\tparams: ", m->u.serverKeyExchange.key, "\n");
		bs = bytesPrint(bs, be, "\tsignature: ", m->u.serverKeyExchange.signature, "\n");
		break;
	case HClientKeyExchange:
		bs = seprint(bs, be, "HClientKeyExchange\n");
		bs = bytesPrint(bs, be, "\tkey: ", m->u.clientKeyExchange.key, "\n");
		break;
	case HFinished:
		bs = seprint(bs, be, "HFinished\n");
		for(i=0; i<m->u.finished.n; i++)
			bs = seprint(bs, be, "%.2x", m->u.finished.verify[i]);
		bs = seprint(bs, be, "\n");
		break;
	}
	USED(bs);
	return buf;
}

static void
tlsError(TlsConnection *c, int err, char *fmt, ...)
{
	char msg[512];
	va_list arg;

	va_start(arg, fmt);
	vseprint(msg, msg+sizeof(msg), fmt, arg);
	va_end(arg);
	if(c->trace)
		c->trace("tlsError: %s\n", msg);
	else if(c->erred)
		fprint(2, "double error: %r, %s", msg);
	else
		werrstr("tls: local %s", msg);
	c->erred = 1;
	fprint(c->ctl, "alert %d", err);
}

// commit to specific version number
static int
setVersion(TlsConnection *c, int version)
{
	if(c->verset || version > MaxProtoVersion || version < MinProtoVersion)
		return -1;
	if(version > c->version)
		version = c->version;
	switch(version) {
	case SSL3Version:
		c->finished.n = SSL3FinishedLen;
		return -1;
	case TLS10Version:
	case TLS11Version:
	case TLS12Version:
		c->finished.n = TLSFinishedLen;
		break;
	default:
		return -1;
	}
	c->version = version;
	c->verset = 1;
	return fprint(c->ctl, "version 0x%x", version);
}

// confirm that received Finished message matches the expected value
static int
finishedMatch(TlsConnection *c, Finished *f)
{
	return memcmp(f->verify, c->finished.verify, f->n) == 0;
}

// free memory associated with TlsConnection struct
//		(but don't close the TLS channel itself)
static void
tlsConnectionFree(TlsConnection *c)
{
	tlsSecClose(c->sec);
	freebytes(c->sid);
	freebytes(c->cert);
	freebytes(c->Ys);
	memset(c, 0, sizeof *c);
	free(c);
}


//================= cipher choices ========================

static int weakCipher[CipherMax] =
{
	1,	/* TLS_NULL_WITH_NULL_NULL */
	1,	/* TLS_RSA_WITH_NULL_MD5 */
	1,	/* TLS_RSA_WITH_NULL_SHA */
	1,	/* TLS_RSA_EXPORT_WITH_RC4_40_MD5 */
	1,	/* TLS_RSA_WITH_RC4_128_MD5 */
	1,	/* TLS_RSA_WITH_RC4_128_SHA */
	1,	/* TLS_RSA_EXPORT_WITH_RC2_CBC_40_MD5 */
	0,	/* TLS_RSA_WITH_IDEA_CBC_SHA */
	1,	/* TLS_RSA_EXPORT_WITH_DES40_CBC_SHA */
	0,	/* TLS_RSA_WITH_DES_CBC_SHA */
	0,	/* TLS_RSA_WITH_3DES_EDE_CBC_SHA */
	1,	/* TLS_DH_DSS_EXPORT_WITH_DES40_CBC_SHA */
	0,	/* TLS_DH_DSS_WITH_DES_CBC_SHA */
	0,	/* TLS_DH_DSS_WITH_3DES_EDE_CBC_SHA */
	1,	/* TLS_DH_RSA_EXPORT_WITH_DES40_CBC_SHA */
	0,	/* TLS_DH_RSA_WITH_DES_CBC_SHA */
	0,	/* TLS_DH_RSA_WITH_3DES_EDE_CBC_SHA */
	1,	/* TLS_DHE_DSS_EXPORT_WITH_DES40_CBC_SHA */
	0,	/* TLS_DHE_DSS_WITH_DES_CBC_SHA */
	0,	/* TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA */
	1,	/* TLS_DHE_RSA_EXPORT_WITH_DES40_CBC_SHA */
	0,	/* TLS_DHE_RSA_WITH_DES_CBC_SHA */
	0,	/* TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA */
	1,	/* TLS_DH_anon_EXPORT_WITH_RC4_40_MD5 */
	1,	/* TLS_DH_anon_WITH_RC4_128_MD5 */
	1,	/* TLS_DH_anon_EXPORT_WITH_DES40_CBC_SHA */
	1,	/* TLS_DH_anon_WITH_DES_CBC_SHA */
	1,	/* TLS_DH_anon_WITH_3DES_EDE_CBC_SHA */
};

static int
setAlgs(TlsConnection *c, int a)
{
	int i;

	for(i = 0; i < nelem(cipherAlgs); i++){
		if(cipherAlgs[i].tlsid == a){
			c->cipher = a;
			c->enc = cipherAlgs[i].enc;
			c->digest = cipherAlgs[i].digest;
			c->nsecret = cipherAlgs[i].nsecret;
			c->sha384 = cipherAlgs[i].sha384;
			if(c->nsecret > MaxKeyData)
				return 0;
			return 1;
		}
	}
	return 0;
}

static int
okCipher(Ints *cv)
{
	int weak, i, j, c;

	weak = 1;
	for(i = 0; i < cv->len; i++) {
		c = cv->data[i];
		if(c >= CipherMax)
			weak = 0;
		else
			weak &= weakCipher[c];
		for(j = 0; j < nelem(cipherAlgs); j++)
			if(cipherAlgs[j].ok && cipherAlgs[j].tlsid == c){
				if(isECDSA(c))
					break;	/* server has no ecdsa key */
				if(isDHE(c))
					break;	/* server has no dhe key */
				return c;
			}
	}
	if(weak)
		return -2;
	return -1;
}

static int
okCompression(Bytes *cv)
{
	int i, j, c;

	for(i = 0; i < cv->len; i++) {
		c = cv->data[i];
		for(j = 0; j < nelem(compressors); j++) {
			if(compressors[j] == c)
				return c;
		}
	}
	return -1;
}

static Lock	ciphLock;
static int	nciphers;

static int
initCiphers(void)
{
	enum {MaxAlgF = 1024, MaxAlgs = 10};
	char s[MaxAlgF], *flds[MaxAlgs];
	int i, j, n, ok;

	lock(&ciphLock);
	if(nciphers){
		unlock(&ciphLock);
		return nciphers;
	}
	j = open("#a/tls/encalgs", OREAD);
	if(j < 0){
		werrstr("can't open #a/tls/encalgs: %r");
		unlock(&ciphLock);
		return 0;
	}
	n = read(j, s, MaxAlgF-1);
	close(j);
	if(n <= 0){
		werrstr("nothing in #a/tls/encalgs: %r");
		unlock(&ciphLock);
		return 0;
	}
	s[n] = 0;
	n = getfields(s, flds, MaxAlgs, 1, " \t\r\n");
	for(i = 0; i < nelem(cipherAlgs); i++){
		ok = 0;
		for(j = 0; j < n; j++){
			if(strcmp(cipherAlgs[i].enc, flds[j]) == 0){
				ok = 1;
				break;
			}
		}
		cipherAlgs[i].ok = ok;
	}

	j = open("#a/tls/hashalgs", OREAD);
	if(j < 0){
		werrstr("can't open #a/tls/hashalgs: %r");
		unlock(&ciphLock);
		return 0;
	}
	n = read(j, s, MaxAlgF-1);
	close(j);
	if(n <= 0){
		werrstr("nothing in #a/tls/hashalgs: %r");
		unlock(&ciphLock);
		return 0;
	}
	s[n] = 0;
	n = getfields(s, flds, MaxAlgs, 1, " \t\r\n");
	for(i = 0; i < nelem(cipherAlgs); i++){
		ok = 0;
		for(j = 0; j < n; j++){
			if(strcmp(cipherAlgs[i].digest, flds[j]) == 0){
				ok = 1;
				break;
			}
		}
		cipherAlgs[i].ok &= ok;
		if(cipherAlgs[i].ok)
			nciphers++;
	}
	unlock(&ciphLock);
	return nciphers;
}

static Ints*
makeciphers(void)
{
	Ints *is;
	int i, j;

	is = newints(nciphers + 1);
	j = 0;
	for(i = 0; i < nelem(cipherAlgs); i++){
		if(cipherAlgs[i].ok)
			is->data[j++] = cipherAlgs[i].tlsid;
	}
	is->data[j++] = TLS_EMPTY_RENEGOTIATION_INFO_SCSV;
	is->len = j;
	return is;
}



//================= security functions ========================

// given X.509 certificate, set up connection to factotum
//	for using corresponding private key
static AuthRpc*
factotum_rsa_open(uchar *cert, int certlen)
{
	int afd;
	char *s;
	mpint *pub = nil;
	RSApub *rsapub;
	AuthRpc *rpc;

	// start talking to factotum
	if((afd = open("/mnt/factotum/rpc", ORDWR)) < 0)
		return nil;
	if((rpc = auth_allocrpc(afd)) == nil){
		close(afd);
		return nil;
	}
	s = "proto=rsa service=tls role=client";
	if(auth_rpc(rpc, "start", s, strlen(s)) != ARok){
		factotum_rsa_close(rpc);
		return nil;
	}

	// roll factotum keyring around to match certificate
	rsapub = X509toRSApub(cert, certlen, nil, 0);
	if(rsapub == nil){
		factotum_rsa_close(rpc);
		return nil;
	}
	while(1){
		if(auth_rpc(rpc, "read", nil, 0) != ARok){
			factotum_rsa_close(rpc);
			rpc = nil;
			goto done;
		}
		pub = strtomp(rpc->arg, nil, 16, nil);
		assert(pub != nil);
		if(mpcmp(pub,rsapub->n) == 0)
			break;
	}
done:
	mpfree(pub);
	rsapubfree(rsapub);
	return rpc;
}

static mpint*
factotum_rsa_decrypt(AuthRpc *rpc, mpint *cipher)
{
	char *p;
	int rv;

	if((p = mptoa(cipher, 16, nil, 0)) == nil)
		return nil;
	rv = auth_rpc(rpc, "write", p, strlen(p));
	free(p);
	if(rv != ARok || auth_rpc(rpc, "read", nil, 0) != ARok)
		return nil;
	mpfree(cipher);
	return strtomp(rpc->arg, nil, 16, nil);
}

static void
factotum_rsa_close(AuthRpc*rpc)
{
	if(!rpc)
		return;
	close(rpc->afd);
	auth_freerpc(rpc);
}

static void
tlsPmd5(uchar *buf, int nbuf, uchar *key, int nkey, uchar *label, int nlabel, uchar *seed0, int nseed0, uchar *seed1, int nseed1)
{
	uchar ai[MD5dlen], tmp[MD5dlen];
	int i, n;
	MD5state *s;

	// generate a1
	s = hmac_md5(label, nlabel, key, nkey, nil, nil);
	s = hmac_md5(seed0, nseed0, key, nkey, nil, s);
	hmac_md5(seed1, nseed1, key, nkey, ai, s);

	while(nbuf > 0) {
		s = hmac_md5(ai, MD5dlen, key, nkey, nil, nil);
		s = hmac_md5(label, nlabel, key, nkey, nil, s);
		s = hmac_md5(seed0, nseed0, key, nkey, nil, s);
		hmac_md5(seed1, nseed1, key, nkey, tmp, s);
		n = MD5dlen;
		if(n > nbuf)
			n = nbuf;
		for(i = 0; i < n; i++)
			buf[i] ^= tmp[i];
		buf += n;
		nbuf -= n;
		hmac_md5(ai, MD5dlen, key, nkey, tmp, nil);
		memmove(ai, tmp, MD5dlen);
	}
}

static void
tlsPsha1(uchar *buf, int nbuf, uchar *key, int nkey, uchar *label, int nlabel, uchar *seed0, int nseed0, uchar *seed1, int nseed1)
{
	uchar ai[SHA1dlen], tmp[SHA1dlen];
	int i, n;
	SHAstate *s;

	// generate a1
	s = hmac_sha1(label, nlabel, key, nkey, nil, nil);
	s = hmac_sha1(seed0, nseed0, key, nkey, nil, s);
	hmac_sha1(seed1, nseed1, key, nkey, ai, s);

	while(nbuf > 0) {
		s = hmac_sha1(ai, SHA1dlen, key, nkey, nil, nil);
		s = hmac_sha1(label, nlabel, key, nkey, nil, s);
		s = hmac_sha1(seed0, nseed0, key, nkey, nil, s);
		hmac_sha1(seed1, nseed1, key, nkey, tmp, s);
		n = SHA1dlen;
		if(n > nbuf)
			n = nbuf;
		for(i = 0; i < n; i++)
			buf[i] ^= tmp[i];
		buf += n;
		nbuf -= n;
		hmac_sha1(ai, SHA1dlen, key, nkey, tmp, nil);
		memmove(ai, tmp, SHA1dlen);
	}
}

static void
tlsPsha2_256(uchar *buf, int nbuf, uchar *key, int nkey, uchar *label, int nlabel, uchar *seed, int nseed)
{
	uchar ai[SHA2_256dlen], tmp[SHA2_256dlen];
	int n;
	SHAstate *s;

	// generate a1
	s = hmac_sha2_256(label, nlabel, key, nkey, nil, nil);
	hmac_sha2_256(seed, nseed, key, nkey, ai, s);

	while(nbuf > 0) {
		s = hmac_sha2_256(ai, SHA2_256dlen, key, nkey, nil, nil);
		s = hmac_sha2_256(label, nlabel, key, nkey, nil, s);
		hmac_sha2_256(seed, nseed, key, nkey, tmp, s);
		n = SHA2_256dlen;
		if(n > nbuf)
			n = nbuf;
		memmove(buf, tmp, n);
		buf += n;
		nbuf -= n;
		hmac_sha2_256(ai, SHA2_256dlen, key, nkey, tmp, nil);
		memmove(ai, tmp, SHA2_256dlen);
	}
}

// fill buf with md5(args)^sha1(args)
static void
tlsPRF(uchar *buf, int nbuf, uchar *key, int nkey, char *label, uchar *seed0, int nseed0, uchar *seed1, int nseed1)
{
	int i;
	int nlabel = strlen(label);
	int n = (nkey + 1) >> 1;

	for(i = 0; i < nbuf; i++)
		buf[i] = 0;
	tlsPmd5(buf, nbuf, key, n, (uchar*)label, nlabel, seed0, nseed0, seed1, nseed1);
	tlsPsha1(buf, nbuf, key+nkey-n, n, (uchar*)label, nlabel, seed0, nseed0, seed1, nseed1);
}

void
tls12PRF(uchar *buf, int nbuf, uchar *key, int nkey, char *label, uchar *seed0, int nseed0, uchar *seed1, int nseed1)
{
	uchar seed[2*RandomSize];
	int nlabel = strlen(label);

	memmove(seed, seed0, nseed0);
	memmove(seed+nseed0, seed1, nseed1);
	tlsPsha2_256(buf, nbuf, key, nkey, (uchar*)label, nlabel, seed, nseed0+nseed1);
}

static void
tlsPsha2_384(uchar *buf, int nbuf, uchar *key, int nkey, uchar *label, int nlabel, uchar *seed, int nseed)
{
	uchar ai[SHA2_384dlen], tmp[SHA2_384dlen];
	int n;
	SHAstate *s;

	// generate a1
	s = hmac_sha2_384(label, nlabel, key, nkey, nil, nil);
	hmac_sha2_384(seed, nseed, key, nkey, ai, s);

	while(nbuf > 0) {
		s = hmac_sha2_384(ai, SHA2_384dlen, key, nkey, nil, nil);
		s = hmac_sha2_384(label, nlabel, key, nkey, nil, s);
		hmac_sha2_384(seed, nseed, key, nkey, tmp, s);
		n = SHA2_384dlen;
		if(n > nbuf)
			n = nbuf;
		memmove(buf, tmp, n);
		buf += n;
		nbuf -= n;
		hmac_sha2_384(ai, SHA2_384dlen, key, nkey, tmp, nil);
		memmove(ai, tmp, SHA2_384dlen);
	}
}

static void
tls12PRF384(uchar *buf, int nbuf, uchar *key, int nkey, char *label, uchar *seed0, int nseed0, uchar *seed1, int nseed1)
{
	uchar seed[2*RandomSize];
	int nlabel = strlen(label);

	memmove(seed, seed0, nseed0);
	memmove(seed+nseed0, seed1, nseed1);
	tlsPsha2_384(buf, nbuf, key, nkey, (uchar*)label, nlabel, seed, nseed0+nseed1);
}

/*
 * for setting server session id's
 */
static Lock	sidLock;
static long	maxSid = 1;

/* the keys are verified to have the same public components
 * and to function correctly with pkcs 1 encryption and decryption. */
static TlsSec*
tlsSecInits(int cvers, uchar *csid, int ncsid, uchar *crandom, uchar *ssid, int *nssid, uchar *srandom)
{
	TlsSec *sec = emalloc(sizeof(*sec));

	USED(csid); USED(ncsid);  // ignore csid for now

	memmove(sec->crandom, crandom, RandomSize);
	sec->clientVers = cvers;

	put32(sec->srandom, time(0));
	genrandom(sec->srandom+4, RandomSize-4);
	memmove(srandom, sec->srandom, RandomSize);

	/*
	 * make up a unique sid: use our pid, and and incrementing id
	 * can signal no sid by setting nssid to 0.
	 */
	memset(ssid, 0, SidSize);
	put32(ssid, getpid());
	lock(&sidLock);
	put32(ssid+4, maxSid++);
	unlock(&sidLock);
	*nssid = SidSize;
	return sec;
}

static int
tlsSecSecrets(TlsSec *sec, int vers, uchar *epm, int nepm, uchar *kd, int nkd)
{
	if(epm != nil){
		if(setVers(sec, vers) < 0)
			goto Err;
		serverMasterSecret(sec, epm, nepm);
	}else if(sec->vers != vers){
		werrstr("mismatched session versions");
		goto Err;
	}
	setSecrets(sec, kd, nkd);
	return 0;
Err:
	sec->ok = -1;
	return -1;
}

static TlsSec*
tlsSecInitc(int cvers, uchar *crandom)
{
	TlsSec *sec = emalloc(sizeof(*sec));
	sec->clientVers = cvers;
	put32(sec->crandom, time(0));
	genrandom(sec->crandom+4, RandomSize-4);
	memmove(crandom, sec->crandom, RandomSize);
	return sec;
}

static int
tlsSecSecretc(TlsSec *sec, uchar *sid, int nsid, uchar *srandom, uchar *cert, int ncert, int vers, uchar **epm, int *nepm, uchar *kd, int nkd)
{
	RSApub *pub;

	pub = nil;

	USED(sid);
	USED(nsid);
	
	memmove(sec->srandom, srandom, RandomSize);

	if(setVers(sec, vers) < 0)
		goto Err;

	pub = X509toRSApub(cert, ncert, nil, 0);
	if(pub == nil){
		werrstr("invalid x509/rsa certificate");
		goto Err;
	}
	if(clientMasterSecret(sec, pub, epm, nepm) < 0)
		goto Err;
	rsapubfree(pub);
	setSecrets(sec, kd, nkd);
	return 0;

Err:
	if(pub != nil)
		rsapubfree(pub);
	sec->ok = -1;
	return -1;
}

static int
tlsSecFinished(TlsSec *sec, HandHash hs, uchar *fin, int nfin, int isclient)
{
	if(sec->nfin != nfin){
		sec->ok = -1;
		werrstr("invalid finished exchange");
		return -1;
	}
	hs.md5.malloced = 0;
	hs.sha1.malloced = 0;
	hs.sha2_256.malloced = 0;
	hs.sha2_384.malloced = 0;
	if(sec->setFinished == nil ){
		sec->ok = -1;
		werrstr("nil sec->setFinished in tlsSecFinished");
		return -1;
	}
	(*sec->setFinished)(sec, hs, fin, isclient);
	return 1;
}

static void
tlsSecOk(TlsSec *sec)
{
	if(sec->ok == 0)
		sec->ok = 1;
}

static void
tlsSecKill(TlsSec *sec)
{
	if(!sec)
		return;
	factotum_rsa_close(sec->rpc);
	sec->ok = -1;
}

static void
tlsSecClose(TlsSec *sec)
{
	if(!sec)
		return;
	factotum_rsa_close(sec->rpc);
	mpfree(sec->ec.Q.x);
	mpfree(sec->ec.Q.y);
	mpfree(sec->ec.Q.d);
	if(sec->ec.dom.p != nil)
		ecdomfree(&sec->ec.dom);
	dh_finish(&sec->dh, nil);
	free(sec->server);
	free(sec);
}

static int
setVers(TlsSec *sec, int v)
{
	switch(v){
	case SSL3Version:
		sec->setFinished = sslSetFinished;
		sec->nfin = SSL3FinishedLen;
		sec->prf = sslPRF;
		break;
	case TLS10Version:
	case TLS11Version:
		sec->setFinished = tlsSetFinished;
		sec->nfin = TLSFinishedLen;
		sec->prf = tlsPRF;
		break;
	case TLS12Version:
		sec->nfin = TLSFinishedLen;
		if(sec->sha384){
			sec->setFinished = tls12SetFinished384;
			sec->prf = tls12PRF384;
		}else{
			sec->setFinished = tls12SetFinished;
			sec->prf = tls12PRF;
		}
		break;
	default:
		werrstr("invalid version");
		sec->setFinished = nil;
		sec->prf = nil;
		return -1;
	}
	sec->vers = v;
	return 0;
}

/*
 * generate secret keys from the master secret.
 *
 * different crypto selections will require different amounts
 * of key expansion and use of key expansion data,
 * but it's all generated using the same function.
 */
static void
setSecrets(TlsSec *sec, uchar *kd, int nkd)
{
	if (sec->prf == nil) {
		werrstr("nil sec->prf in setSecrets");
		return;
	}
	(*sec->prf)(kd, nkd, sec->sec, MasterSecretSize, "key expansion",
			sec->srandom, RandomSize, sec->crandom, RandomSize);
}

/*
 * set the master secret from the pre-master secret.
 */
static void
setMasterSecret(TlsSec *sec, Bytes *pm)
{
	(*sec->prf)(sec->sec, MasterSecretSize, pm->data, pm->len, "master secret",
			sec->crandom, RandomSize, sec->srandom, RandomSize);
}

static void
serverMasterSecret(TlsSec *sec, uchar *epm, int nepm)
{
	Bytes *pm;

	pm = pkcs1_decrypt(sec, epm, nepm);

	// if the client messed up, just continue as if everything is ok,
	// to prevent attacks to check for correctly formatted messages.
	if(pm == nil || pm->len != MasterSecretSize || get16(pm->data) != sec->clientVers){
		freebytes(pm);
		pm = newbytes(MasterSecretSize);
		genrandom(pm->data, MasterSecretSize);
	}
	setMasterSecret(sec, pm);
	memset(pm->data, 0, pm->len);
	freebytes(pm);
}

static int
clientMasterSecret(TlsSec *sec, RSApub *pub, uchar **epm, int *nepm)
{
	Bytes *pm, *key;

	pm = newbytes(MasterSecretSize);
	put16(pm->data, sec->clientVers);
	genrandom(pm->data+2, MasterSecretSize - 2);

	setMasterSecret(sec, pm);

	key = pkcs1_encrypt(pm, pub, 2);
	memset(pm->data, 0, pm->len);
	freebytes(pm);
	if(key == nil){
		werrstr("tls pkcs1_encrypt failed");
		return -1;
	}

	*nepm = key->len;
	*epm = malloc(*nepm);
	if(*epm == nil){
		freebytes(key);
		werrstr("out of memory");
		return -1;
	}
	memmove(*epm, key->data, *nepm);

	freebytes(key);

	return 1;
}

// the DER DigestInfo prefix of an RSASSA-PKCS1-v1_5 sha256 signature
static uchar sha256digestinfo[] = {
	0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,
	0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20,
};

// digest the signed portion of a tls 1.2 server key exchange
static void
dhParamsDigest(TlsSec *sec, Bytes *par, uchar *digest)
{
	DigestState *s;

	s = sha2_256(sec->crandom, RandomSize, nil, nil);
	s = sha2_256(sec->srandom, RandomSize, nil, s);
	sha2_256(par->data, par->len, digest, s);
}

// sign a sha256 digest with the host private key held in factotum
static Bytes*
pkcs1_sign(TlsSec *sec, uchar *digest, int digestlen)
{
	Bytes *sig;
	mpint *x, *y;
	uchar *eb;
	int modlen, padlen, infolen;

	infolen = sizeof(sha256digestinfo) + digestlen;
	modlen = (mpsignif(sec->rsapub->n)+7)/8;
	if(modlen < infolen + 11)
		return nil;
	eb = emalloc(modlen);
	eb[0] = 0;
	eb[1] = 1;
	padlen = modlen - 3 - infolen;
	memset(eb+2, 0xff, padlen);
	eb[2+padlen] = 0;
	memmove(eb+3+padlen, sha256digestinfo, sizeof(sha256digestinfo));
	memmove(eb+3+padlen+sizeof(sha256digestinfo), digest, digestlen);
	x = betomp(eb, modlen, nil);
	free(eb);
	y = factotum_rsa_decrypt(sec->rpc, x);
	if(y == nil)
		return nil;
	sig = newbytes(modlen);
	mptober(y, sig->data, modlen);
	mpfree(y);
	return sig;
}

// verify a server key exchange signature over a sha256 digest
static int
pkcs1_verify(RSApub *pk, Bytes *sig, uchar *digest, int digestlen)
{
	uchar *buf, info[sizeof(sha256digestinfo)+SHA2_256dlen];
	int len, infolen, nlen, i;
	mpint *x;

	infolen = sizeof(sha256digestinfo) + digestlen;
	if(infolen > sizeof(info))
		return -1;
	memmove(info, sha256digestinfo, sizeof(sha256digestinfo));
	memmove(info+sizeof(sha256digestinfo), digest, digestlen);

	x = betomp(sig->data, sig->len, nil);
	mpexp(x, pk->ek, pk->n, x);
	buf = nil;
	len = mptobe(x, nil, 0, &buf);
	mpfree(x);

	// the leading 0x00 is dropped by mptobe, so expect 01 ff..ff 00 DigestInfo
	nlen = (mpsignif(pk->n)-1)/8;
	i = -1;
	if(len == nlen && buf[0] == 1){
		for(i = 1; i < len && buf[i] == 0xff; i++)
			;
		if(i >= len || buf[i] != 0)
			i = -1;
		else
			i++;
	}
	if(i < 0 || len-i != infolen || memcmp(buf+i, info, infolen) != 0){
		free(buf);
		return -1;
	}
	free(buf);
	return 0;
}

// signed key exchange hooks, filled in by the ECDHE and DHE patches.
// with neither enabled isECDHE/isDHE are always false and the stubs are
// never reached, so the handshake stays pure RSA key exchange.
static int
isECDHE(int tlsid)
{
	switch(tlsid){
	case TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA:
	case TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA:
	case TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256:
	case TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256:
	case TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384:
	case TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256:
	case TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA:
	case TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA:
	case TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256:
	case TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256:
	case TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384:
	case TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305:
		return 1;
	}
	return 0;
}

static int
isECDSA(int tlsid)
{
	switch(tlsid){
	case TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA:
	case TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA:
	case TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256:
	case TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256:
	case TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384:
	case TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305:
		return 1;
	}
	return 0;
}

static int
isDHE(int tlsid)
{
	switch(tlsid){
	case TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA:
	case TLS_DHE_RSA_WITH_AES_128_CBC_SHA:
	case TLS_DHE_RSA_WITH_AES_256_CBC_SHA:
	case TLS_DHE_RSA_WITH_AES_128_CBC_SHA256:
	case TLS_DHE_RSA_WITH_AES_128_GCM_SHA256:
	case TLS_DHE_RSA_WITH_CHACHA20_POLY1305:
		return 1;
	}
	return 0;
}

// server: pick a curve offered by the client, leaving the choice in sec
static int
tlsSecECDHEs0(TlsSec *sec, Ints *curves)
{
	Namedcurve *nc;
	int i;

	sec->nc = nil;
	for(i = 0; curves != nil && i < curves->len && sec->nc == nil; i++)
		for(nc = namedcurves; nc < &namedcurves[nelem(namedcurves)]; nc++)
			if(nc->tlsid == curves->data[i]){
				sec->nc = nc;
				break;
			}
	if(sec->nc == nil)
		return -1;
	return 0;
}

// server: generate an ephemeral key and return the params to be signed
static Bytes*
tlsSecECDHEs1(TlsSec *sec, int *curve)
{
	ECdomain *dom = &sec->ec.dom;
	ECpriv *Q = &sec->ec.Q;
	Bytes *par;
	int n;

	if(sec->nc == nil)
		return nil;
	*curve = sec->nc->tlsid;
	if(sec->nc->tlsid == X25519){
		par = newbytes(1+2+1+32);
		par->data[0] = 3;		/* named_curve */
		put16(par->data+1, X25519);
		par->data[3] = 32;
		curve25519_dh_new(sec->X, par->data+4);
		return par;
	}
	ecdominit(dom, sec->nc->init);
	memset(Q, 0, sizeof(*Q));
	Q->x = mpnew(0);
	Q->y = mpnew(0);
	Q->d = mpnew(0);
	ecgen(dom, Q);
	n = 1 + 2*((mpsignif(dom->p)+7)/8);
	par = newbytes(1+2+1+n);
	par->data[0] = 3;			/* named_curve */
	put16(par->data+1, sec->nc->tlsid);
	n = ecencodepub(dom, Q, par->data+4, par->len-4);
	par->data[3] = n;
	par->len = 1+2+1+n;
	return par;
}

// server: compute the shared secret from the client's ephemeral key
static int
tlsSecECDHEs2(TlsSec *sec, Bytes *Yc)
{
	ECdomain *dom = &sec->ec.dom;
	ECpriv *Q = &sec->ec.Q;
	ECpoint K;
	ECpub *Y;
	Bytes *Z;
	int n;

	if(Yc == nil){
		werrstr("no public key");
		return -1;
	}
	if(sec->nc->tlsid == X25519){
		if(Yc->len != 32){
			werrstr("bad public key");
			return -1;
		}
		Z = newbytes(32);
		if(!curve25519_dh_finish(sec->X, Yc->data, Z->data)){
			werrstr("unlucky shared key");
			freebytes(Z);
			return -1;
		}
		setMasterSecret(sec, Z);
		memset(Z->data, 0, Z->len);
		freebytes(Z);
		return 0;
	}
	if((Y = ecdecodepub(dom, Yc->data, Yc->len)) == nil){
		werrstr("bad public key");
		return -1;
	}
	memset(&K, 0, sizeof(K));
	K.x = mpnew(0);
	K.y = mpnew(0);
	ecmul(dom, Y, Q->d, &K);
	n = (mpsignif(dom->p)+7)/8;
	Z = newbytes(n);
	mptober(K.x, Z->data, n);
	setMasterSecret(sec, Z);
	memset(Z->data, 0, Z->len);
	freebytes(Z);
	mpfree(K.x);
	mpfree(K.y);
	ecpubfree(Y);
	return 0;
}

// client: compute the shared secret and return our ephemeral key
static Bytes*
tlsSecECDHEc(TlsSec *sec, int curve, Bytes *par)
{
	ECdomain *dom = &sec->ec.dom;
	ECpriv *Q = &sec->ec.Q;
	ECpub *pub;
	ECpoint K;
	Namedcurve *nc;
	Bytes *Yc, *Z, *Ys;
	int n;

	if(par == nil || par->len < 4)
		return nil;
	/* the point follows the 4-byte ECParams header */
	Ys = makebytes(par->data+4, par->len-4);
	if(curve == X25519){
		if(Ys->len != 32){
			freebytes(Ys);
			return nil;
		}
		Yc = newbytes(32);
		curve25519_dh_new(sec->X, Yc->data);
		Z = newbytes(32);
		if(!curve25519_dh_finish(sec->X, Ys->data, Z->data)){
			freebytes(Yc);
			freebytes(Z);
			freebytes(Ys);
			return nil;
		}
		setMasterSecret(sec, Z);
		memset(Z->data, 0, Z->len);
		freebytes(Z);
		freebytes(Ys);
		return Yc;
	}
	for(nc = namedcurves; nc->tlsid != curve;)
		if(++nc >= &namedcurves[nelem(namedcurves)]){
			freebytes(Ys);
			return nil;
		}
	ecdominit(dom, nc->init);
	pub = ecdecodepub(dom, Ys->data, Ys->len);
	freebytes(Ys);
	if(pub == nil)
		return nil;
	memset(Q, 0, sizeof(*Q));
	Q->x = mpnew(0);
	Q->y = mpnew(0);
	Q->d = mpnew(0);
	memset(&K, 0, sizeof(K));
	K.x = mpnew(0);
	K.y = mpnew(0);
	ecgen(dom, Q);
	ecmul(dom, pub, Q->d, &K);
	n = (mpsignif(dom->p)+7)/8;
	Z = newbytes(n);
	mptober(K.x, Z->data, n);
	setMasterSecret(sec, Z);
	memset(Z->data, 0, Z->len);
	freebytes(Z);
	Yc = newbytes(1 + 2*n);
	Yc->len = ecencodepub(dom, Q, Yc->data, Yc->len);
	mpfree(K.x);
	mpfree(K.y);
	ecpubfree(pub);
	return Yc;
}

// server: generate the ephemeral DH params to be signed
static Bytes*
tlsSecDHEs1(TlsSec *sec, int *curve)
{
	USED(sec); USED(curve);
	return nil;
}

// server: compute the shared secret from the client's DH public value
static int
tlsSecDHEs2(TlsSec *sec, Bytes *Yc)
{
	USED(sec); USED(Yc);
	werrstr("DHE not supported");
	return -1;
}

// client: compute the shared secret and return our DH public value
static Bytes*
tlsSecDHEc(TlsSec *sec, Bytes *par)
{
	DHstate *dh = &sec->dh;
	mpint *P, *G, *Y, *K;
	Bytes *Yc, *Z;
	uchar *p, *e;
	int n, len;

	if(par == nil)
		return nil;
	P = G = Y = K = nil;
	Yc = nil;
	p = par->data;
	e = p + par->len;
	if(e - p < 2)
		goto Out;
	len = get16(p);
	p += 2;
	if(e - p < len || len <= 1024/8)	/* reject logjam-weak primes */
		goto Out;
	P = betomp(p, len, nil);
	p += len;
	if(e - p < 2)
		goto Out;
	len = get16(p);
	p += 2;
	if(e - p < len)
		goto Out;
	G = betomp(p, len, nil);
	p += len;
	if(e - p < 2)
		goto Out;
	len = get16(p);
	p += 2;
	if(e - p < len)
		goto Out;
	Y = betomp(p, len, nil);
	if(dh_new(dh, P, nil, G) == nil)
		goto Out;
	n = (mpsignif(P)+7)/8;
	Yc = newbytes(n);
	mptober(dh->y, Yc->data, n);
	K = dh_finish(dh, Y);	/* zeros dh */
	if(K == nil){
		freebytes(Yc);
		Yc = nil;
		goto Out;
	}
	Z = newbytes(n);
	mptober(K, Z->data, n);
	setMasterSecret(sec, Z);
	memset(Z->data, 0, Z->len);
	freebytes(Z);
Out:
	mpfree(K);
	mpfree(Y);
	mpfree(G);
	mpfree(P);
	return Yc;
}

static void
sslSetFinished(TlsSec *sec, HandHash hs, uchar *finished, int isClient)
{
	DigestState *s;
	uchar h0[MD5dlen], h1[SHA1dlen], pad[48];
	char *label;

	if(isClient)
		label = "CLNT";
	else
		label = "SRVR";

	md5((uchar*)label, 4, nil, &hs.md5);
	md5(sec->sec, MasterSecretSize, nil, &hs.md5);
	memset(pad, 0x36, 48);
	md5(pad, 48, nil, &hs.md5);
	md5(nil, 0, h0, &hs.md5);
	memset(pad, 0x5C, 48);
	s = md5(sec->sec, MasterSecretSize, nil, nil);
	s = md5(pad, 48, nil, s);
	md5(h0, MD5dlen, finished, s);

	sha1((uchar*)label, 4, nil, &hs.sha1);
	sha1(sec->sec, MasterSecretSize, nil, &hs.sha1);
	memset(pad, 0x36, 40);
	sha1(pad, 40, nil, &hs.sha1);
	sha1(nil, 0, h1, &hs.sha1);
	memset(pad, 0x5C, 40);
	s = sha1(sec->sec, MasterSecretSize, nil, nil);
	s = sha1(pad, 40, nil, s);
	sha1(h1, SHA1dlen, finished + MD5dlen, s);
}

// fill "finished" arg with md5(args)^sha1(args)
static void
tlsSetFinished(TlsSec *sec, HandHash hs, uchar *finished, int isClient)
{
	uchar h0[MD5dlen], h1[SHA1dlen];
	char *label;

	// get current hash value, but allow further messages to be hashed in
	md5(nil, 0, h0, &hs.md5);
	sha1(nil, 0, h1, &hs.sha1);

	if(isClient)
		label = "client finished";
	else
		label = "server finished";
	if (sec->prf == nil) {
		werrstr("nil sec->prf in tlsSetFinished");
		return;
	}
	(*sec->prf)(finished, TLSFinishedLen, sec->sec, MasterSecretSize, label, h0, MD5dlen, h1, SHA1dlen);
}

// fill "finished" arg with sha256(args)
static void
tls12SetFinished(TlsSec *sec, HandHash hs, uchar *finished, int isClient)
{
	uchar h[SHA2_256dlen];
	char *label;

	// get current hash value, but allow further messages to be hashed in
	sha2_256(nil, 0, h, &hs.sha2_256);

	if(isClient)
		label = "client finished";
	else
		label = "server finished";
	tlsPsha2_256(finished, TLSFinishedLen, sec->sec, MasterSecretSize, (uchar*)label, strlen(label), h, SHA2_256dlen);
}

// fill "finished" arg with sha384(args)
static void
tls12SetFinished384(TlsSec *sec, HandHash hs, uchar *finished, int isClient)
{
	uchar h[SHA2_384dlen];
	char *label;

	// get current hash value, but allow further messages to be hashed in
	sha2_384(nil, 0, h, &hs.sha2_384);

	if(isClient)
		label = "client finished";
	else
		label = "server finished";
	tlsPsha2_384(finished, TLSFinishedLen, sec->sec, MasterSecretSize, (uchar*)label, strlen(label), h, SHA2_384dlen);
}

static void
sslPRF(uchar *buf, int nbuf, uchar *key, int nkey, char *label, uchar *seed0, int nseed0, uchar *seed1, int nseed1)
{
	DigestState *s;
	uchar sha1dig[SHA1dlen], md5dig[MD5dlen], tmp[26];
	int i, n, len;

	USED(label);
	len = 1;
	while(nbuf > 0){
		if(len > 26)
			return;
		for(i = 0; i < len; i++)
			tmp[i] = 'A' - 1 + len;
		s = sha1(tmp, len, nil, nil);
		s = sha1(key, nkey, nil, s);
		s = sha1(seed0, nseed0, nil, s);
		sha1(seed1, nseed1, sha1dig, s);
		s = md5(key, nkey, nil, nil);
		md5(sha1dig, SHA1dlen, md5dig, s);
		n = MD5dlen;
		if(n > nbuf)
			n = nbuf;
		memmove(buf, md5dig, n);
		buf += n;
		nbuf -= n;
		len++;
	}
}

static mpint*
bytestomp(Bytes* bytes)
{
	mpint* ans;

	ans = betomp(bytes->data, bytes->len, nil);
	return ans;
}

/*
 * Convert mpint* to Bytes, putting high order byte first.
 */
static Bytes*
mptobytes(mpint* big)
{
	int n, m;
	uchar *a;
	Bytes* ans;

	a = nil;
	n = (mpsignif(big)+7)/8;
	m = mptobe(big, nil, n, &a);
	ans = makebytes(a, m);
	if(a != nil)
		free(a);
	return ans;
}

// Do RSA computation on block according to key, and pad
// result on left with zeros to make it modlen long.
static Bytes*
rsacomp(Bytes* block, RSApub* key, int modlen)
{
	mpint *x, *y;
	Bytes *a, *ybytes;
	int ylen;

	x = bytestomp(block);
	y = rsaencrypt(key, x, nil);
	mpfree(x);
	ybytes = mptobytes(y);
	ylen = ybytes->len;

	if(ylen < modlen) {
		a = newbytes(modlen);
		memset(a->data, 0, modlen-ylen);
		memmove(a->data+modlen-ylen, ybytes->data, ylen);
		freebytes(ybytes);
		ybytes = a;
	}
	else if(ylen > modlen) {
		// assume it has leading zeros (mod should make it so)
		a = newbytes(modlen);
		memmove(a->data, ybytes->data, modlen);
		freebytes(ybytes);
		ybytes = a;
	}
	mpfree(y);
	return ybytes;
}

// encrypt data according to PKCS#1, /lib/rfc/rfc2437 9.1.2.1
static Bytes*
pkcs1_encrypt(Bytes* data, RSApub* key, int blocktype)
{
	Bytes *pad, *eb, *ans;
	int i, dlen, padlen, modlen;

	modlen = (mpsignif(key->n)+7)/8;
	dlen = data->len;
	if(modlen < 12 || dlen > modlen - 11)
		return nil;
	padlen = modlen - 3 - dlen;
	pad = newbytes(padlen);
	genrandom(pad->data, padlen);
	for(i = 0; i < padlen; i++) {
		if(blocktype == 0)
			pad->data[i] = 0;
		else if(blocktype == 1)
			pad->data[i] = 255;
		else if(pad->data[i] == 0)
			pad->data[i] = 1;
	}
	eb = newbytes(modlen);
	eb->data[0] = 0;
	eb->data[1] = blocktype;
	memmove(eb->data+2, pad->data, padlen);
	eb->data[padlen+2] = 0;
	memmove(eb->data+padlen+3, data->data, dlen);
	ans = rsacomp(eb, key, modlen);
	freebytes(eb);
	freebytes(pad);
	return ans;
}

// decrypt data according to PKCS#1, with given key.
// expect a block type of 2.
static Bytes*
pkcs1_decrypt(TlsSec *sec, uchar *epm, int nepm)
{
	Bytes *eb, *ans = nil;
	int i, modlen;
	mpint *x, *y;

	modlen = (mpsignif(sec->rsapub->n)+7)/8;
	if(nepm != modlen)
		return nil;
	x = betomp(epm, nepm, nil);
	y = factotum_rsa_decrypt(sec->rpc, x);
	if(y == nil)
		return nil;
	eb = mptobytes(y);
	if(eb->len < modlen){ // pad on left with zeros
		ans = newbytes(modlen);
		memset(ans->data, 0, modlen-eb->len);
		memmove(ans->data+modlen-eb->len, eb->data, eb->len);
		freebytes(eb);
		eb = ans;
	}
	if(eb->data[0] == 0 && eb->data[1] == 2) {
		for(i = 2; i < modlen; i++)
			if(eb->data[i] == 0)
				break;
		if(i < modlen - 1)
			ans = makebytes(eb->data+i+1, modlen-(i+1));
	}
	if (eb != ans)			/* not freed above? */
		freebytes(eb);
	return ans;
}


//================= general utility functions ========================

static void *
emalloc(int n)
{
	void *p;
	if(n==0)
		n=1;
	p = malloc(n);
	if(p == nil){
		exits("out of memory");
	}
	memset(p, 0, n);
	return p;
}

static void *
erealloc(void *ReallocP, int ReallocN)
{
	if(ReallocN == 0)
		ReallocN = 1;
	if(!ReallocP)
		ReallocP = emalloc(ReallocN);
	else if(!(ReallocP = realloc(ReallocP, ReallocN))){
		exits("out of memory");
	}
	return(ReallocP);
}

static void
put32(uchar *p, u32int x)
{
	p[0] = x>>24;
	p[1] = x>>16;
	p[2] = x>>8;
	p[3] = x;
}

static void
put24(uchar *p, int x)
{
	p[0] = x>>16;
	p[1] = x>>8;
	p[2] = x;
}

static void
put16(uchar *p, int x)
{
	p[0] = x>>8;
	p[1] = x;
}

static u32int
get32(uchar *p)
{
	return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
}

static int
get24(uchar *p)
{
	return (p[0]<<16)|(p[1]<<8)|p[2];
}

static int
get16(uchar *p)
{
	return (p[0]<<8)|p[1];
}

#define OFFSET(x, s) offsetof(s, x)

/*
 * malloc and return a new Bytes structure capable of
 * holding len bytes. (len >= 0)
 * Used to use crypt_malloc, which aborts if malloc fails.
 */
static Bytes*
newbytes(int len)
{
	Bytes* ans;

	ans = (Bytes*)malloc(OFFSET(data[0], Bytes) + len);
	ans->len = len;
	return ans;
}

/*
 * newbytes(len), with data initialized from buf
 */
static Bytes*
makebytes(uchar* buf, int len)
{
	Bytes* ans;

	ans = newbytes(len);
	memmove(ans->data, buf, len);
	return ans;
}

static void
freebytes(Bytes* b)
{
	if(b != nil)
		free(b);
}

/* len is number of ints */
static Ints*
newints(int len)
{
	Ints* ans;

	ans = (Ints*)malloc(OFFSET(data[0], Ints) + len*sizeof(int));
	ans->len = len;
	return ans;
}

static Ints*
makeints(int* buf, int len)
{
	Ints* ans;

	ans = newints(len);
	if(len > 0)
		memmove(ans->data, buf, len*sizeof(int));
	return ans;
}

static void
freeints(Ints* b)
{
	if(b != nil)
		free(b);
}
