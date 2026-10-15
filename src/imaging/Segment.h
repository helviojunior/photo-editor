// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Seleção por pincel: traço sobre a foto -> máscara do objeto pintado.
//
// Quem acha o objeto é o SAM 2.1 tiny (Segment Anything 2, Meta, Apache-2.0)
// em ONNX, rodando em CPU pelo ONNX Runtime. O modelo tem duas metades:
//
// * o **encoder** lê a foto inteira e devolve os embeddings (~1,3 s numa CPU
//   comum). Roda UMA vez por foto: o resultado fica num cache em memória;
// * o **decoder** recebe pontos/caixa e devolve 3 máscaras candidatas em ~25 ms.
//
// O traço da pessoa COBRE o objeto (como no Lightroom). O SAM, com um ponto,
// tende a devolver uma PARTE (o rosto, o braço); por isso cada traço vira
// vários prompts (caixa, pontos no miolo, os dois) e, das máscaras candidatas,
// vence a que melhor coincide (IoU) com a área pintada.
//
// Memória: o encoder chega a ~1,2 GB de pico (sem o arena do ONNX Runtime, que
// seguraria esse pico para sempre). Um mutex serializa as execuções: duas fotos
// ao mesmo tempo dobrariam o pico.
//
// Sem PHOTOEDITOR_HAVE_ONNX (ou sem os arquivos do modelo), available() é
// falso e select() devolve a própria área pintada com borda macia — o mesmo
// que o Python fazia com ``smart`` desligado.
//
// Porte 1:1 de backend/photoeditor/imaging/segment.py (branch legacy).
// Máscaras binárias são cv::Mat CV_8UC1 com 0/255 (o ``bool`` do numpy).

#include <QString>

#include <opencv2/core.hpp>

#include <memory>
#include <vector>

namespace pe::imaging {

inline constexpr int kSegmentInputSide = 1024;
/// ~16 MB por foto: cobre ir e voltar entre as vizinhas sem reprocessar.
inline constexpr int kSegmentEmbedCache = 4;
inline constexpr int kSegmentThreads = 4;
inline constexpr const char *kSegmentEncoder = "vision_encoder.onnx";
inline constexpr const char *kSegmentDecoder = "prompt_encoder_mask_decoder.onnx";

/// Área coberta por um traço (CV_8UC1, 0/255, do tamanho ``size``): ``points``
/// em fração da largura/altura e ``radius`` em fração do lado maior (o tamanho
/// do pincel na tela).
cv::Mat paint(cv::Size size, const std::vector<cv::Point2d> &points, double radius);

/// Borda macia (CV_32FC1 em [0, 1]) de uma máscara binária. Com o objeto
/// detectado (``smart``), o guided filter encosta a borda no contorno real da
/// foto ``rgb`` (CV_8UC3); o pincel puro só é suavizado.
cv::Mat refine(const cv::Mat &rgb, const cv::Mat &mask, bool smart = true);

/// ``n`` pontos (x, y) espalhados no MIOLO da área (longe da borda do traço) —
/// ``_inner_points`` do Python.
std::vector<cv::Point2f> innerPoints(const cv::Mat &region, int n);

/// Interseção sobre união de duas máscaras binárias — ``_iou``.
double maskIou(const cv::Mat &a, const cv::Mat &b);

/// Tira ilhas soltas (menores que ``minFrac`` da maior parte) e tapa furos
/// pequenos — ``_clean``. Devolve uma máscara nova (CV_8UC1, 0/255).
cv::Mat cleanMask(const cv::Mat &mask, double minFrac = 0.02);

/// O SAM com cache de embeddings. Thread-safe: um mutex serializa as
/// execuções do modelo. As sessões são criadas na primeira seleção.
class Segmenter
{
public:
    /// ``modelDir`` = pasta com vision_encoder.onnx e
    /// prompt_encoder_mask_decoder.onnx (+ os .onnx_data).
    explicit Segmenter(const QString &modelDir);
    ~Segmenter();

    Segmenter(const Segmenter &) = delete;
    Segmenter &operator=(const Segmenter &) = delete;

    /// Modelo presente e ONNX Runtime compilado. Sem ele, o pincel vale como
    /// está (a área pintada vira a máscara), sem detecção de objeto.
    static bool available(const QString &modelDir);
    /// available(modelDir) e as sessões não falharam ao carregar.
    bool available() const;

    /// Calcula os embeddings antes do primeiro traço (ao entrar no modo).
    /// ``key`` = foto + versão do arquivo. Falso se o modelo não está disponível.
    bool warm(const QString &key, const cv::Mat &rgb);

    /// Um traço -> máscara macia (CV_32FC1 em [0, 1], do tamanho de ``rgb``).
    /// Com ``smart`` (e o modelo disponível), cada parte conexa do traço vira o
    /// objeto que ela cobre; sem, a própria área pintada.
    cv::Mat select(const QString &key, const cv::Mat &rgb, const std::vector<cv::Point2d> &points,
                   double radius, bool smart = true);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace pe::imaging
