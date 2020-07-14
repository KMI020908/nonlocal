#ifndef FINITE_ELEMENT_QUBIC_SERENDIPITY_ELEMENT_HPP
#define FINITE_ELEMENT_QUBIC_SERENDIPITY_ELEMENT_HPP

// Базисные функции элемента взяты из статьи:
// Астионенко И.А., Литвиненко Е.И., Хомченко А.Н. Конструирование многопараметрических полиномов на бикубическом элементе серендипова семейства.
// Из данной статьи был выбран 13-параметрический базис в силу его простоты и единственности.

#include "geometry_2d.hpp"

namespace metamath::finite_element {

template<class T>
class qubic_serendipity : public geometry_2d<T, rectangle_element_geometry> {
public:
    T get_parameter() const noexcept { return _p; }
    void set_parameter(const T p) noexcept { _p = p; }

protected:
    using geometry_2d<T, rectangle_element_geometry>::xi;
    using geometry_2d<T, rectangle_element_geometry>::eta;

    // В серендиповой аппроксимации высших порядков возникает проблема с негативизмом стандартного базиса в угловых узлах.
    // Для этого вводится специальный параметр p, который позволяет её избежать.
    // В сущности p является значением интеграла по области элемента от угловой функции. Значение интегралов от промежуточных функций есть (1-4p)/2.
    T _p = -0.5; // Значение по умолчанию даёт нам классический вариант кубических серендиповых элементов.
    static constexpr symdiff::variable<2> p{}; // В силу особенностей вычисления выражений, дополнительному параметру необходимо завести дополнительную переменную.

    explicit qubic_serendipity() = default;

    // Нумерация узлов на кубическом серендиповом элементе: 9---8---7---6
    //                                                      |           |
    //                                                      10          5
    //                                                      |           |
    //                                                      11          4
    //                                                      |           |
    //                                                      0---1---2---3
    static constexpr std::array<std::array<T, 2>, 12> nodes = {    -1.,    -1.,
                                                                -1./3.,    -1.,
                                                                 1./3.,    -1.,
                                                                    1.,    -1.,
                                                                    1., -1./3.,
                                                                    1.,  1./3.,
                                                                    1.,     1.,
                                                                 1./3.,     1.,
                                                                -1./3.,     1.,
                                                                   -1.,     1.,
                                                                   -1.,  1./3.,
                                                                   -1., -1./3. };

    // Базисные функции в локальной системе координат имеют вид:
    // N_i = 1/32 (1 + xi_i xi)(1 + eta_i eta)[9(xi^2 + eta^2) + (18p+9)(xi_i xi eta_i eta - xi_i xi - eta_i eta) + 18p-1], xi_i = +-1, eta_i = +-1, i = 0,3,6,9,
    // N_i = 9/64 (1 -  xi^2)(1 + eta_i eta)[18  xi_i  xi + (2p+1) eta_i eta - 1 + 2p], xi_i = +-1/3, eta_i = +-1  , i = 1,2,7,8,
    // N_i = 9/64 (1 - eta^2)(1 +  xi_i  xi)[18 eta_i eta + (2p+1)  xi_i  xi - 1 + 2p], xi_i = +-1  , eta_i = +-1/3, i = 4,5,10,11.
    static constexpr auto basis = std::make_tuple(
         (1.-xi)    * (1.-eta)     * (0.28125*(xi*xi+eta*eta + (2.00000*p+1.000000)*(xi*eta+xi+eta)) + 0.56250*p - 0.031250),
        -(1.-xi*xi) * (1.-eta)     * (0.84375*xi             + (0.28125*p+0.140625)*eta              + 0.28125*p - 0.140625),
         (1.-xi*xi) * (1.-eta)     * (0.84375*xi             - (0.28125*p+0.140625)*eta              - 0.28125*p + 0.140625),
         (1.+xi)    * (1.-eta)     * (0.28125*(xi*xi+eta*eta - (2.00000*p+1.000000)*(xi*eta+xi-eta)) + 0.56250*p - 0.031250),
        -(1.+xi)    * (1.-eta*eta) * (0.84375*eta            - (0.28125*p+0.140625)*xi               + 0.28125*p - 0.140625),
         (1.+xi)    * (1.-eta*eta) * (0.84375*eta            + (0.28125*p+0.140625)*xi               - 0.28125*p + 0.140625),
         (1.+xi)    * (1.+eta)     * (0.28125*(xi*xi+eta*eta + (2.00000*p+1.000000)*(xi*eta-xi-eta)) + 0.56250*p - 0.031250),
         (1.-xi*xi) * (1.+eta)     * (0.84375*xi             + (0.28125*p+0.140625)*eta              - 0.28125*p + 0.140625),
        -(1.-xi*xi) * (1.+eta)     * (0.84375*xi             - (0.28125*p+0.140625)*eta              + 0.28125*p - 0.140625),
         (1.-xi)    * (1.+eta)     * (0.28125*(xi*xi+eta*eta - (2.00000*p+1.000000)*(xi*eta-xi+eta)) + 0.56250*p - 0.031250),
         (1.-xi)    * (1.-eta*eta) * (0.84375*eta            - (0.28125*p+0.140625)*xi               - 0.28125*p + 0.140625),
        -(1.-xi)    * (1.-eta*eta) * (0.84375*eta            + (0.28125*p+0.140625)*xi               + 0.28125*p - 0.140625)
    );

    static inline const std::array<std::function<T(const std::array<T, 3>&)>, 12>
        N    = symdiff::to_function<T, 3>(basis),
        Nxi  = symdiff::to_function<T, 3>(symdiff::derivative<xi>(basis)),
        Neta = symdiff::to_function<T, 3>(symdiff::derivative<eta>(basis));
};

}

#endif