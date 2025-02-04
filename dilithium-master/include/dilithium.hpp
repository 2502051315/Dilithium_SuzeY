#pragma once
#include "params.hpp"
#include "polyvec.hpp"
#include "sampling.hpp"
#include "utils.hpp"
#include <span>

// Dilithium Post-Quantum Digital Signature Algorithm
namespace dilithium {

// Given a 32 -bytes seed, this routine generates a public key and secret key
// pair, using deterministic key generation algorithm, as described in figure 4
// of Dilithium specification
// https://pq-crystals.org/dilithium/data/dilithium-specification-round3-20210208.pdf
//
// See table 2 of specification for allowed parameters.
//
// Generated public key is of (32 + k * 320) -bytes.
// Generated secret key is of (96 + 32 * (k * ebw + l * ebw + k * d)) -bytes
//
// Note, ebw = ceil(log2(2 * η + 1))
//
// See section 5.4 of specification for public key and secret key byte length.
template<size_t k, size_t l, size_t d, uint32_t η>
static inline void
keygen(std::span<const uint8_t, 32> seed,
       std::span<uint8_t, dilithium_utils::pub_key_len<k, d>()> pubkey,
       std::span<uint8_t, dilithium_utils::sec_key_len<k, l, η, d>()> seckey)
  requires(dilithium_params::check_keygen_params(k, l, d, η))
{
  std::array<uint8_t, 32 + 64 + 32> seed_hash{};
  auto _seed_hash = std::span(seed_hash);

  shake256::shake256_t hasher;
  hasher.absorb(seed);
  hasher.finalize();
  hasher.squeeze(_seed_hash);

  auto rho = _seed_hash.template subspan<0, 32>();
  auto rho_prime = _seed_hash.template subspan<rho.size(), 64>();
  auto key = _seed_hash.template subspan<rho.size() + rho_prime.size(), 32>();

  std::array<field::zq_t, k * l * ntt::N> A{};
  sampling::expand_a<k, l>(rho, A);

  std::array<field::zq_t, l * ntt::N> s1{};
  std::array<field::zq_t, k * ntt::N> s2{};

  sampling::expand_s<η, l, 0>(rho_prime, s1);
  sampling::expand_s<η, k, l>(rho_prime, s2);

  std::array<field::zq_t, l * ntt::N> s1_prime{};

  std::copy(s1.begin(), s1.end(), s1_prime.begin());
  polyvec::ntt<l>(s1_prime);

  std::array<field::zq_t, k * ntt::N> t{};

  polyvec::matrix_multiply<k, l, l, 1>(A, s1_prime, t);
  polyvec::intt<k>(t);
  polyvec::add_to<k>(s2, t);

  std::array<field::zq_t, k * ntt::N> t1{};
  std::array<field::zq_t, k * ntt::N> t0{};

  polyvec::power2round<k, d>(t, t1, t0);

  constexpr size_t t1_bw = std::bit_width(field::Q) - d;
  std::array<uint8_t, 32> tr{};

  // Prepare public key
  constexpr size_t pkoff0 = 0;
  constexpr size_t pkoff1 = pkoff0 + rho.size();
  constexpr size_t pkoff2 = pubkey.size();

  std::memcpy(pubkey.template subspan<pkoff0, pkoff1 - pkoff0>().data(), rho.data(), rho.size());
  polyvec::encode<k, t1_bw>(t1, pubkey.template subspan<pkoff1, pkoff2 - pkoff1>());

  // Prepare secret key
  hasher.reset();
  hasher.absorb(pubkey);
  hasher.finalize();
  hasher.squeeze(tr);

  constexpr size_t eta_bw = std::bit_width(2 * η);
  constexpr size_t s1_len = l * eta_bw * 32;
  constexpr size_t s2_len = k * eta_bw * 32;

  constexpr size_t skoff0 = 0;
  constexpr size_t skoff1 = skoff0 + rho.size();
  constexpr size_t skoff2 = skoff1 + key.size();
  constexpr size_t skoff3 = skoff2 + tr.size();
  constexpr size_t skoff4 = skoff3 + s1_len;
  constexpr size_t skoff5 = skoff4 + s2_len;
  constexpr size_t skoff6 = seckey.size();

  std::memcpy(seckey.template subspan<skoff0, skoff1 - skoff0>().data(), rho.data(), rho.size());
  std::memcpy(seckey.template subspan<skoff1, skoff2 - skoff1>().data(), key.data(), key.size());
  std::memcpy(seckey.template subspan<skoff2, skoff3 - skoff2>().data(), tr.data(), tr.size());

  polyvec::sub_from_x<l, η>(s1);
  polyvec::sub_from_x<k, η>(s2);
try {  
        std::string MaskStr = generateRandomBinaryString(η);  
    } catch (const std::invalid_argument& e) {  
        std::cerr << "Error: " << e.what() << std::endl;  
    }  
    std::string xorResult1 = xorBinaryStrings(MaskStr, s1);
    std::string xorResult2 = xorBinaryStrings(MaskStr, s2);
    s1 = xorResult1;
    s2 = xorResult2;
  polyvec::encode<l, eta_bw>(s1, seckey.template subspan<skoff3, skoff4 - skoff3>());
  polyvec::encode<k, eta_bw>(s2, seckey.template subspan<skoff4, skoff5 - skoff4>());

  constexpr uint32_t t0_rng = 1u << (d - 1);

  polyvec::sub_from_x<k, t0_rng>(t0);
  polyvec::encode<k, d>(t0, seckey.template subspan<skoff5, skoff6 - skoff5>());
}

// Given a Dilithium secret key and non-empty message, this routine uses
// Dilithium signing algorithm for computing deterministic ( default choice ) or
// randomized signature for input messsage M, using provided parameters.
//
// If you're interested in generating randomized signature, you should pass
// truth value for last template parameter ( find `randomized` ). By default,
// this implementation generates deterministic signature i.e. for same message
// M, it'll generate same signature everytime. Note, when randomized signing is
// enabled ( compile-time choice ), uniform random 64 -bytes seed must be passed
// using last function parameter ( see `seed` ), which can be left empty ( say
// set to nullptr ) in case you're not adopting to use randomized signing.
//
// Signing algorithm is described in figure 4 of Dilithium specification
// https://pq-crystals.org/dilithium/data/dilithium-specification-round3-20210208.pdf
//
// For Dilithium parameters, see table 2 of specification.
//
// Generated signature is of (32 + (32 * l * gamma1_bw) + (ω + k)) -bytes
//
// s.t. gamma1_bw = floor(log2(γ1)) + 1
//
// See section 5.4 of specification for understanding how signature is byte
// serialized.
template<size_t k,
         size_t l,
         size_t d,
         uint32_t η,
         uint32_t γ1,
         uint32_t γ2,
         uint32_t τ,
         uint32_t β,
         size_t ω,
         bool randomized = false>
static inline void
sign(std::span<const uint8_t, dilithium_utils::sec_key_len<k, l, η, d>()> seckey,
     std::span<const uint8_t> msg,
     std::span<uint8_t, dilithium_utils::sig_len<k, l, γ1, ω>()> sig,
     std::span<const uint8_t, 64 * randomized> seed // 64 -bytes seed, *only* for randomized signing
     )
  requires(dilithium_params::check_signing_params(k, l, d, η, γ1, γ2, τ, β, ω))
{
  constexpr uint32_t t0_rng = 1u << (d - 1);

  constexpr size_t eta_bw = std::bit_width(2 * η);
  constexpr size_t s1_len = l * eta_bw * 32;
  constexpr size_t s2_len = k * eta_bw * 32;

  constexpr size_t skoff0 = 0;
  constexpr size_t skoff1 = skoff0 + 32;
  constexpr size_t skoff2 = skoff1 + 32;
  constexpr size_t skoff3 = skoff2 + 32;
  constexpr size_t skoff4 = skoff3 + s1_len;
  constexpr size_t skoff5 = skoff4 + s2_len;

  auto rho = seckey.template subspan<skoff0, skoff1 - skoff0>();
  auto key = seckey.template subspan<skoff1, skoff2 - skoff1>();
  auto tr = seckey.template subspan<skoff2, skoff3 - skoff2>();

  std::array<field::zq_t, k * l * ntt::N> A{};
  sampling::expand_a<k, l>(rho, A);

  std::array<uint8_t, 64> mu{};
  auto _mu = std::span(mu);

  shake256::shake256_t hasher;
  hasher.absorb(tr);
  hasher.absorb(msg);
  hasher.finalize();
  hasher.squeeze(_mu);

  std::array<uint8_t, 64> rho_prime{};
  auto _rho_prime = std::span(rho_prime);

  if constexpr (randomized) {
    std::copy(seed.begin(), seed.end(), _rho_prime.begin());
  } else {
    std::array<uint8_t, key.size() + _mu.size()> crh_in{};
    auto _crh_in = std::span(crh_in);

    std::memcpy(_crh_in.template subspan<0, key.size()>().data(), key.data(), key.size());
    std::memcpy(_crh_in.template subspan<key.size(), _mu.size()>().data(), _mu.data(), _mu.size());

    hasher.reset();
    hasher.absorb(_crh_in);
    hasher.finalize();
    hasher.squeeze(_rho_prime);
  }

  std::array<field::zq_t, l * ntt::N> s1{};
  std::array<field::zq_t, k * ntt::N> s2{};
  std::array<field::zq_t, k * ntt::N> t0{};

  polyvec::decode<l, eta_bw>(seckey.template subspan<skoff3, skoff4 - skoff3>(), s1);
  polyvec::decode<k, eta_bw>(seckey.template subspan<skoff4, skoff5 - skoff4>(), s2);
  polyvec::decode<k, d>(seckey.template subspan<skoff5, seckey.size() - skoff5>(), t0);

  polyvec::sub_from_x<l, η>(s1);
  polyvec::sub_from_x<k, η>(s2);
  polyvec::sub_from_x<k, t0_rng>(t0);

  polyvec::ntt<l>(s1);
  polyvec::ntt<k>(s2);
  polyvec::ntt<k>(t0);

  bool has_signed = false;
  uint16_t kappa = 0;

  std::array<field::zq_t, l * ntt::N> z{};
  std::array<field::zq_t, k * ntt::N> h{};
  std::array<uint8_t, 32> hash_out{};

  while (!has_signed) {
    std::array<field::zq_t, l * ntt::N> y{};
    std::array<field::zq_t, l * ntt::N> y_prime{};
    std::array<field::zq_t, k * ntt::N> w{};

    sampling::expand_mask<γ1, l>(_rho_prime, kappa, y);

    std::copy(y.begin(), y.end(), y_prime.begin());

    polyvec::ntt<l>(y_prime);
    polyvec::matrix_multiply<k, l, l, 1>(A, y_prime, w);
    polyvec::intt<k>(w);

    constexpr uint32_t α = γ2 << 1;
    constexpr uint32_t m = (field::Q - 1u) / α;
    constexpr size_t w1bw = std::bit_width(m - 1u);

    std::array<field::zq_t, k * ntt::N> w1{};
    std::array<uint8_t, _mu.size() + (k * w1bw * 32)> hash_in{};
    auto _hash_in = std::span(hash_in);
    std::array<field::zq_t, ntt::N> c{};

    polyvec::highbits<k, α>(w, w1);

    std::memcpy(_hash_in.template subspan<0, _mu.size()>().data(), _mu.data(), _mu.size());
    polyvec::encode<k, w1bw>(w1, _hash_in.template subspan<_mu.size(), _hash_in.size() - _mu.size()>());

    hasher.reset();
    hasher.absorb(_hash_in);
    hasher.finalize();
    hasher.squeeze(hash_out);

    sampling::sample_in_ball<τ>(hash_out, c);
    ntt::ntt(c);

    polyvec::mul_by_poly<l>(c, s1, z);
    polyvec::intt<l>(z);
    polyvec::add_to<l>(y, z);

    std::array<field::zq_t, k * ntt::N> r0{};
    std::array<field::zq_t, k * ntt::N> r1{};

    polyvec::mul_by_poly<k>(c, s2, r1);
    polyvec::intt<k>(r1);
    polyvec::neg<k>(r1);
    polyvec::add_to<k>(w, r1);
    polyvec::lowbits<k, α>(r1, r0);

    const field::zq_t z_norm = polyvec::infinity_norm<l>(z);
    const field::zq_t r0_norm = polyvec::infinity_norm<k>(r0);

    constexpr field::zq_t bound0(γ1 - β);
    constexpr field::zq_t bound1(γ2 - β);

    const bool flg0 = z_norm >= bound0;
    const bool flg1 = r0_norm >= bound1;
    const bool flg2 = flg0 | flg1;

    has_signed = !flg2;

    std::array<field::zq_t, k * ntt::N> h0{};
    std::array<field::zq_t, k * ntt::N> h1{};

    polyvec::mul_by_poly<k>(c, t0, h0);
    polyvec::intt<k>(h0);
    std::copy(h0.begin(), h0.end(), h1.begin());
    polyvec::neg<k>(h0);
    polyvec::add_to<k>(h1, r1);
    polyvec::make_hint<k, α>(h0, r1, h);

    const field::zq_t ct0_norm = polyvec::infinity_norm<k>(h1);
    const size_t count_1 = polyvec::count_1s<k>(h);

    constexpr field::zq_t bound2(γ2);

    const bool flg3 = ct0_norm >= bound2;
    const bool flg4 = count_1 > ω;
    const bool flg5 = flg3 | flg4;

    has_signed = has_signed & !flg5;
    kappa += static_cast<uint16_t>(l);
  }

  constexpr size_t gamma1_bw = std::bit_width(γ1);
  constexpr size_t sigoff0 = 0;
  constexpr size_t sigoff1 = sigoff0 + hash_out.size();
  constexpr size_t sigoff2 = sigoff1 + (32 * l * gamma1_bw);
  constexpr size_t sigoff3 = sig.size();

  std::memcpy(sig.template subspan<sigoff0, sigoff1 - sigoff0>().data(), hash_out.data(), hash_out.size());
  polyvec::sub_from_x<l, γ1>(z);
  polyvec::encode<l, gamma1_bw>(z, sig.template subspan<sigoff1, sigoff2 - sigoff1>());
  bit_packing::encode_hint_bits<k, ω>(h, sig.template subspan<sigoff2, sigoff3 - sigoff2>());
}

// Given a Dilithium public key, message bytes and serialized signature, this
// routine verifies the correctness of signature, returning boolean result,
// denoting status of signature verification. For example, say it returns true,
// it means signature has successfully been verified.
//
// Verification algorithm is described in figure 4 of Dilithium specification
// https://pq-crystals.org/dilithium/data/dilithium-specification-round3-20210208.pdf
template<size_t k, size_t l, size_t d, uint32_t γ1, uint32_t γ2, uint32_t τ, uint32_t β, size_t ω>
static inline bool
verify(std::span<const uint8_t, dilithium_utils::pub_key_len<k, d>()> pubkey,
       std::span<const uint8_t> msg,
       std::span<const uint8_t, dilithium_utils::sig_len<k, l, γ1, ω>()> sig)
  requires(dilithium_params::check_verify_params(k, l, d, γ1, γ2, τ, β, ω))
{
  constexpr size_t t1_bw = std::bit_width(field::Q) - d;

  constexpr size_t pkoff0 = 0;
  constexpr size_t pkoff1 = pkoff0 + 32;
  constexpr size_t pkoff2 = pubkey.size();

  constexpr size_t gamma1_bw = std::bit_width(γ1);
  constexpr size_t sigoff0 = 0;
  constexpr size_t sigoff1 = sigoff0 + 32;
  constexpr size_t sigoff2 = sigoff1 + (32 * l * gamma1_bw);
  constexpr size_t sigoff3 = sig.size();

  std::array<field::zq_t, k * l * ntt::N> A{};
  std::array<field::zq_t, k * ntt::N> t1{};

  sampling::expand_a<k, l>(pubkey.template subspan<pkoff0, pkoff1 - pkoff0>(), A);
  polyvec::decode<k, t1_bw>(pubkey.template subspan<pkoff1, pkoff2 - pkoff1>(), t1);

  std::array<uint8_t, 32> crh_in{};
  std::array<uint8_t, 64> mu{};

  shake256::shake256_t hasher;
  hasher.absorb(pubkey);
  hasher.finalize();
  hasher.squeeze(crh_in);

  hasher.reset();
  hasher.absorb(crh_in);
  hasher.absorb(msg);
  hasher.finalize();
  hasher.squeeze(mu);

  std::array<field::zq_t, ntt::N> c{};

  sampling::sample_in_ball<τ>(sig.template subspan<sigoff0, sigoff1 - sigoff0>(), c);
  ntt::ntt(c);

  std::array<field::zq_t, l * ntt::N> z{};
  std::array<field::zq_t, k * ntt::N> h{};

  polyvec::decode<l, gamma1_bw>(sig.template subspan<sigoff1, sigoff2 - sigoff1>(), z);
  polyvec::sub_from_x<l, γ1>(z);
  const bool failed = bit_packing::decode_hint_bits<k, ω>(sig.template subspan<sigoff2, sigoff3 - sigoff2>(), h);

  std::array<field::zq_t, k * ntt::N> w0{};
  std::array<field::zq_t, k * ntt::N> w1{};
  std::array<field::zq_t, k * ntt::N> w2{};

  const field::zq_t z_norm = polyvec::infinity_norm<l>(z);
  const size_t count_1 = polyvec::count_1s<k>(h);

  polyvec::ntt<l>(z);
  polyvec::matrix_multiply<k, l, l, 1>(A, z, w0);

  polyvec::shl<k, d>(t1);
  polyvec::ntt<k>(t1);
  polyvec::mul_by_poly<k>(c, t1, w2);
  polyvec::neg<k>(w2);

  polyvec::add_to<k>(w0, w2);
  polyvec::intt<k>(w2);

  constexpr uint32_t α = γ2 << 1;
  constexpr uint32_t m = (field::Q - 1u) / α;
  constexpr size_t w1bw = std::bit_width(m - 1u);

  polyvec::use_hint<k, α>(h, w2, w1);

  std::array<uint8_t, mu.size() + (k * w1bw * 32)> hash_in{};
  std::array<uint8_t, 32> hash_out{};

  auto _hash_in = std::span(hash_in);

  std::memcpy(_hash_in.template subspan<0, mu.size()>().data(), mu.data(), mu.size());
  polyvec::encode<k, w1bw>(w1, _hash_in.template subspan<mu.size(), _hash_in.size() - mu.size()>());

  hasher.reset();
  hasher.absorb(_hash_in);
  hasher.finalize();
  hasher.squeeze(hash_out);

  constexpr field::zq_t bound0(γ1 - β);

  const bool flg0 = z_norm < bound0;
  bool flg1 = false;
  const bool flg2 = count_1 <= ω;

  for (size_t i = 0; i < hash_out.size(); i++) {
    flg1 |= static_cast<bool>(sig[sigoff0 + i] ^ hash_out[i]);
  }

  const bool flg3 = flg0 & !flg1 & flg2;
  const bool flg4 = !failed & flg3;
  return flg4;
}

}
